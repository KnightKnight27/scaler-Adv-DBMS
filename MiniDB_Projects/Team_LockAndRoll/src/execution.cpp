#include "execution.h"

#include <algorithm>
#include <map>

#include "engine.h"

namespace minidb {

Value eval_expr(const Expr* e, const Schema& schema, const Row& row) {
  switch (e->kind) {
    case ExprKind::IntLit: return Value(e->int_val);
    case ExprKind::StrLit: return Value(e->str_val);
    case ExprKind::BoolLit: return Value(e->bool_val);
    case ExprKind::NullLit: return Value::null();
    case ExprKind::Column: {
      int idx = schema.index_of(e->col_name);
      if (idx < 0) throw DBException("unknown column: " + e->col_name);
      return row[idx];
    }
    case ExprKind::Unary: {
      Value v = eval_expr(e->left.get(), schema, row);
      if (v.is_null()) return Value::null();
      if (e->op == "-" && v.is_int()) return Value(-v.as_int());
      throw DBException("bad unary operand");
    }
    case ExprKind::Agg:
      throw DBException("aggregate not allowed here");
    case ExprKind::Binary: {
      const std::string& op = e->op;
      if (op == "AND" || op == "OR") {
        Value l = eval_expr(e->left.get(), schema, row);
        Value r = eval_expr(e->right.get(), schema, row);
        // null operand counts as false; a non-bool operand is an error, not coerced
        auto truth = [](const Value& v) -> bool {
          if (v.is_null()) return false;
          if (!v.is_bool()) throw DBException("non-boolean operand to AND/OR");
          return v.as_bool();
        };
        bool lb = truth(l), rb = truth(r);
        return Value(op == "AND" ? (lb && rb) : (lb || rb));
      }
      Value l = eval_expr(e->left.get(), schema, row);
      Value r = eval_expr(e->right.get(), schema, row);
      if (op == "+" || op == "-" || op == "*" || op == "/") {
        if (l.is_null() || r.is_null()) return Value::null();
        if (!l.is_int() || !r.is_int()) throw DBException("arithmetic requires integer operands");
        int64_t a = l.as_int(), b = r.as_int();
        if (op == "+") return Value(a + b);
        if (op == "-") return Value(a - b);
        if (op == "*") return Value(a * b);
        if (b == 0) throw DBException("division by zero");
        return Value(a / b);
      }
      // null operand makes a comparison unknown (treated as not-true)
      if (l.is_null() || r.is_null()) return Value::null();
      int c = l.compare(r);
      if (op == "=") return Value(c == 0);
      if (op == "!=") return Value(c != 0);
      if (op == "<") return Value(c < 0);
      if (op == "<=") return Value(c <= 0);
      if (op == ">") return Value(c > 0);
      if (op == ">=") return Value(c >= 0);
      throw DBException("unknown operator " + op);
    }
  }
  return Value::null();
}

std::string output_name(const SelectItem& item, int index) {
  if (!item.alias.empty()) return item.alias;
  if (item.star) return "*";
  const Expr* e = item.expr.get();
  if (e->kind == ExprKind::Column) return e->col_name;
  if (e->kind == ExprKind::Agg) return e->func + "(" + (e->star ? "*" : e->left->col_name) + ")";
  return "col" + std::to_string(index);
}

namespace {

Schema qualify(const Schema& s, const std::string& alias) {
  std::vector<Column> cols;
  for (const Column& c : s.columns()) cols.push_back({alias + "." + c.name, c.type});
  return Schema(std::move(cols));
}

class SeqScanExec : public Executor {
 public:
  SeqScanExec(ExecutionContext ctx, TableInfo* t, std::string alias)
      : ctx_(ctx), table_(t), schema_(qualify(t->schema, alias)) {}
  const Schema& out_schema() const override { return schema_; }
  void open() override {
    ctx_.db->scan_table(ctx_.txn, table_, [&](RID, const Tuple& tup) {
      rows_.push_back(tup.values());
      return true;
    });
  }
  bool next(Row* out) override {
    if (pos_ >= rows_.size()) return false;
    *out = rows_[pos_++];
    return true;
  }

 private:
  ExecutionContext ctx_;
  TableInfo* table_;
  Schema schema_;
  std::vector<Row> rows_;
  size_t pos_ = 0;
};

class IndexScanExec : public Executor {
 public:
  IndexScanExec(ExecutionContext ctx, TableInfo* t, std::string alias, int64_t key)
      : ctx_(ctx), table_(t), schema_(qualify(t->schema, alias)), key_(key) {}
  const Schema& out_schema() const override { return schema_; }
  void open() override {
    Tuple t;
    if (ctx_.db->read_key(ctx_.txn, table_, key_, &t)) rows_.push_back(t.values());
  }
  bool next(Row* out) override {
    if (pos_ >= rows_.size()) return false;
    *out = rows_[pos_++];
    return true;
  }

 private:
  ExecutionContext ctx_;
  TableInfo* table_;
  Schema schema_;
  int64_t key_;
  std::vector<Row> rows_;
  size_t pos_ = 0;
};

class FilterExec : public Executor {
 public:
  FilterExec(std::unique_ptr<Executor> child, ExprPtr pred)
      : child_(std::move(child)), pred_(std::move(pred)) {}
  const Schema& out_schema() const override { return child_->out_schema(); }
  void open() override { child_->open(); }
  bool next(Row* out) override {
    Row r;
    while (child_->next(&r)) {
      Value v = eval_expr(pred_.get(), child_->out_schema(), r);
      if (v.is_bool() && v.as_bool()) {
        *out = std::move(r);
        return true;
      }
    }
    return false;
  }

 private:
  std::unique_ptr<Executor> child_;
  ExprPtr pred_;
};

class NestedLoopJoinExec : public Executor {
 public:
  NestedLoopJoinExec(std::unique_ptr<Executor> l, std::unique_ptr<Executor> r, ExprPtr on)
      : left_(std::move(l)), right_(std::move(r)), on_(std::move(on)) {
    std::vector<Column> cols = left_->out_schema().columns();
    const auto& rc = right_->out_schema().columns();
    cols.insert(cols.end(), rc.begin(), rc.end());
    schema_ = Schema(std::move(cols));
  }
  const Schema& out_schema() const override { return schema_; }
  void open() override {
    left_->open();
    right_->open();
    Row r;
    while (right_->next(&r)) right_rows_.push_back(r);  // materialize the inner side once
    have_left_ = left_->next(&left_row_);
    rpos_ = 0;
  }
  bool next(Row* out) override {
    while (have_left_) {
      while (rpos_ < right_rows_.size()) {
        Row combined = left_row_;
        const Row& rr = right_rows_[rpos_++];
        combined.insert(combined.end(), rr.begin(), rr.end());
        if (!on_ || [&] {
              Value v = eval_expr(on_.get(), schema_, combined);
              return v.is_bool() && v.as_bool();
            }()) {
          *out = std::move(combined);
          return true;
        }
      }
      have_left_ = left_->next(&left_row_);
      rpos_ = 0;
    }
    return false;
  }

 private:
  std::unique_ptr<Executor> left_, right_;
  ExprPtr on_;
  Schema schema_;
  std::vector<Row> right_rows_;
  Row left_row_;
  bool have_left_ = false;
  size_t rpos_ = 0;
};

class ProjectionExec : public Executor {
 public:
  ProjectionExec(std::unique_ptr<Executor> child, std::vector<SelectItem> items)
      : child_(std::move(child)), items_(std::move(items)) {
    std::vector<Column> cols;
    for (size_t i = 0; i < items_.size(); i++) {
      if (items_[i].star) {
        for (const Column& c : child_->out_schema().columns()) cols.push_back(c);
      } else {
        cols.push_back({output_name(items_[i], static_cast<int>(i)), TypeId::INTEGER});
      }
    }
    schema_ = Schema(std::move(cols));
  }
  const Schema& out_schema() const override { return schema_; }
  void open() override { child_->open(); }
  bool next(Row* out) override {
    Row r;
    if (!child_->next(&r)) return false;
    Row res;
    for (const SelectItem& it : items_) {
      if (it.star) {
        for (const Value& v : r) res.push_back(v);
      } else {
        res.push_back(eval_expr(it.expr.get(), child_->out_schema(), r));
      }
    }
    *out = std::move(res);
    return true;
  }

 private:
  std::unique_ptr<Executor> child_;
  std::vector<SelectItem> items_;
  Schema schema_;
};

struct RowLess {
  bool operator()(const Row& a, const Row& b) const {
    for (size_t i = 0; i < a.size() && i < b.size(); i++) {
      int c = a[i].compare(b[i]);
      if (c != 0) return c < 0;
    }
    return a.size() < b.size();
  }
};

class AggregationExec : public Executor {
 public:
  AggregationExec(std::unique_ptr<Executor> child, std::vector<SelectItem> items,
                  std::vector<std::string> group_by)
      : child_(std::move(child)), items_(std::move(items)), group_by_(std::move(group_by)) {
    std::vector<Column> cols;
    for (size_t i = 0; i < items_.size(); i++)
      cols.push_back({output_name(items_[i], static_cast<int>(i)), TypeId::INTEGER});
    schema_ = Schema(std::move(cols));
  }
  const Schema& out_schema() const override { return schema_; }

  struct Acc {
    int64_t count = 0;
    int64_t sum = 0;
    Value minv, maxv;
    bool minmax_init = false;
    bool has_sum = false;
  };
  struct Group {
    Row rep;  // a representative input row, for non-aggregate output columns
    std::vector<Acc> accs;
  };

  void open() override {
    child_->open();
    const Schema& cs = child_->out_schema();
    Row r;
    while (child_->next(&r)) {
      Row key;
      for (const std::string& g : group_by_)
        key.push_back(eval_expr_col(g, cs, r));
      auto it = groups_.find(key);
      if (it == groups_.end()) {
        Group g;
        g.rep = r;
        g.accs.resize(items_.size());
        it = groups_.emplace(key, std::move(g)).first;
      }
      Group& g = it->second;
      for (size_t i = 0; i < items_.size(); i++) {
        const SelectItem& item = items_[i];
        if (item.star || !item.expr || item.expr->kind != ExprKind::Agg) continue;
        const Expr* a = item.expr.get();
        Acc& acc = g.accs[i];
        if (a->star) { acc.count++; continue; }
        Value v = eval_expr(a->left.get(), cs, r);
        if (v.is_null()) continue;
        acc.count++;
        if (v.is_int()) { acc.sum += v.as_int(); acc.has_sum = true; }
        if (!acc.minmax_init || v.compare(acc.minv) < 0) acc.minv = v;
        if (!acc.minmax_init || v.compare(acc.maxv) > 0) acc.maxv = v;
        acc.minmax_init = true;
      }
    }
    // no GROUP BY over zero rows still yields one all-null/zero aggregate row
    if (group_by_.empty() && groups_.empty()) {
      Group g;
      g.accs.resize(items_.size());
      groups_.emplace(Row{}, std::move(g));
    }
    it_ = groups_.begin();
  }

  bool next(Row* out) override {
    if (it_ == groups_.end()) return false;
    const Group& g = it_->second;
    const Schema& cs = child_->out_schema();
    Row res;
    for (size_t i = 0; i < items_.size(); i++) {
      const SelectItem& item = items_[i];
      if (item.expr && item.expr->kind == ExprKind::Agg) {
        const Expr* a = item.expr.get();
        const Acc& acc = g.accs[i];
        if (a->func == "COUNT") res.push_back(Value(acc.count));
        else if (a->func == "SUM") res.push_back(acc.has_sum ? Value(acc.sum) : Value::null());
        else if (a->func == "AVG")
          res.push_back(acc.count ? Value(acc.sum / acc.count) : Value::null());
        else if (a->func == "MIN") res.push_back(acc.minmax_init ? acc.minv : Value::null());
        else if (a->func == "MAX") res.push_back(acc.minmax_init ? acc.maxv : Value::null());
        else res.push_back(Value::null());
      } else {
        res.push_back(eval_expr(item.expr.get(), cs, g.rep));
      }
    }
    ++it_;
    *out = std::move(res);
    return true;
  }

 private:
  static Value eval_expr_col(const std::string& name, const Schema& s, const Row& r) {
    int idx = s.index_of(name);
    if (idx < 0) throw DBException("unknown GROUP BY column: " + name);
    return r[idx];
  }
  std::unique_ptr<Executor> child_;
  std::vector<SelectItem> items_;
  std::vector<std::string> group_by_;
  Schema schema_;
  std::map<Row, Group, RowLess> groups_;
  std::map<Row, Group, RowLess>::iterator it_;
};

}  // namespace

std::unique_ptr<Executor> build_executor(const PlanPtr& plan, ExecutionContext ctx) {
  switch (plan->type) {
    case PlanType::SeqScan:
      return std::make_unique<SeqScanExec>(ctx, plan->table, plan->alias);
    case PlanType::IndexScan:
      return std::make_unique<IndexScanExec>(ctx, plan->table, plan->alias, plan->index_key);
    case PlanType::Filter:
      return std::make_unique<FilterExec>(build_executor(plan->children[0], ctx),
                                          plan->predicate);
    case PlanType::NestedLoopJoin:
      return std::make_unique<NestedLoopJoinExec>(build_executor(plan->children[0], ctx),
                                                  build_executor(plan->children[1], ctx),
                                                  plan->join_on);
    case PlanType::Projection:
      return std::make_unique<ProjectionExec>(build_executor(plan->children[0], ctx),
                                              plan->items);
    case PlanType::Aggregation:
      return std::make_unique<AggregationExec>(build_executor(plan->children[0], ctx),
                                               plan->items, plan->group_by);
  }
  throw DBException("unknown plan node");
}

}  // namespace minidb
