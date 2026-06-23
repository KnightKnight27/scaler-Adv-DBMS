#include "exec/executor.h"
#include <stdexcept>

namespace minidb {

Schema QualifySchema(const Schema &schema, const std::string &table) {
  std::vector<Column> cols;
  for (const Column &c : schema.columns()) {
    Column q = c;
    q.name = table + "." + c.name;
    cols.push_back(q);
  }
  return Schema(std::move(cols));
}

int ResolveColumn(const Schema &schema, const std::string &ref) {
  int match = -1;
  for (size_t i = 0; i < schema.num_columns(); ++i) {
    const std::string &name = schema.column(i).name;  // e.g. "users.id"
    bool hit = (name == ref);
    if (!hit) {  // also match the unqualified suffix ("id")
      size_t dot = name.find('.');
      std::string base = (dot == std::string::npos) ? name : name.substr(dot + 1);
      hit = (base == ref);
    }
    if (hit) {
      if (match != -1) throw std::runtime_error("ambiguous column reference: " + ref);
      match = static_cast<int>(i);
    }
  }
  if (match == -1) throw std::runtime_error("unknown column: " + ref);
  return match;
}

std::vector<BoundPredicate> BindPredicates(const Schema &schema,
                                           const std::vector<Predicate> &preds) {
  std::vector<BoundPredicate> out;
  for (const Predicate &p : preds) {
    int col = ResolveColumn(schema, p.column);
    if (schema.column(col).type != p.value.type) {
      throw std::runtime_error("type mismatch in predicate on '" + p.column + "'");
    }
    out.push_back({col, p.op, p.value});
  }
  return out;
}

bool EvalPredicates(const Tuple &t, const std::vector<BoundPredicate> &preds) {
  for (const BoundPredicate &p : preds) {
    int c = t.value(p.col).Compare(p.value);
    bool ok = false;
    switch (p.op) {
      case CompareOp::EQ: ok = (c == 0); break;
      case CompareOp::NE: ok = (c != 0); break;
      case CompareOp::LT: ok = (c < 0);  break;
      case CompareOp::LE: ok = (c <= 0); break;
      case CompareOp::GT: ok = (c > 0);  break;
      case CompareOp::GE: ok = (c >= 0); break;
    }
    if (!ok) return false;  // conjunction: any false rejects the row
  }
  return true;
}

bool FilterExecutor::Next(Tuple *out) {
  Tuple t;
  while (child_->Next(&t)) {
    if (EvalPredicates(t, preds_)) { *out = t; return true; }
  }
  return false;
}

bool ProjectionExecutor::Next(Tuple *out) {
  Tuple t;
  if (!child_->Next(&t)) return false;
  std::vector<Value> vals;
  vals.reserve(cols_.size());
  for (int c : cols_) vals.push_back(t.value(c));
  *out = Tuple(std::move(vals));
  return true;
}

void NestedLoopJoinExecutor::Init() {
  left_->Init();
  have_left_ = false;
}

bool NestedLoopJoinExecutor::Next(Tuple *out) {
  while (true) {
    if (!have_left_) {
      if (!left_->Next(&left_row_)) return false;  // outer exhausted
      right_->Init();                               // rescan inner per outer row
      have_left_ = true;
    }
    Tuple right_row;
    while (right_->Next(&right_row)) {
      if (left_row_.value(left_col_) == right_row.value(right_col_)) {
        std::vector<Value> vals = left_row_.values();
        const auto &rv = right_row.values();
        vals.insert(vals.end(), rv.begin(), rv.end());
        *out = Tuple(std::move(vals));
        return true;
      }
    }
    have_left_ = false;  // advance to next outer row
  }
}

bool CountExecutor::Next(Tuple *out) {
  if (done_) return false;
  int64_t n = 0;
  Tuple t;
  while (child_->Next(&t)) ++n;
  *out = Tuple({Value::Int(n)});
  done_ = true;
  return true;
}

}  // namespace minidb
