#include "execution/executor.hpp"

#include <algorithm>
#include <stdexcept>

namespace minidb {

int resolve_column(const OutSchema& schema, const std::string& table,
                   const std::string& name) {
    int found = -1;
    for (size_t i = 0; i < schema.size(); ++i) {
        if (schema[i].name != name) continue;
        if (!table.empty() && schema[i].table != table) continue;
        if (found != -1) return -2;   // ambiguous
        found = static_cast<int>(i);
    }
    return found;
}

Value eval_expr(const Expr* e, const Tuple& row, const OutSchema& schema) {
    switch (e->kind) {
        case ExprKind::LITERAL:
            return e->literal;
        case ExprKind::COLUMN: {
            int idx = resolve_column(schema, e->table, e->column);
            if (idx < 0) throw std::runtime_error("unknown/ambiguous column: " + e->column);
            return row[idx];
        }
        case ExprKind::BINARY: {
            if (e->op == BinOp::AND)
                return Value::Int(eval_pred(e->left.get(), row, schema) &&
                                  eval_pred(e->right.get(), row, schema));
            if (e->op == BinOp::OR)
                return Value::Int(eval_pred(e->left.get(), row, schema) ||
                                  eval_pred(e->right.get(), row, schema));
            Value l = eval_expr(e->left.get(), row, schema);
            Value r = eval_expr(e->right.get(), row, schema);
            int c = l.compare(r);
            bool res = false;
            switch (e->op) {
                case BinOp::EQ: res = (c == 0); break;
                case BinOp::NE: res = (c != 0); break;
                case BinOp::LT: res = (c < 0); break;
                case BinOp::LE: res = (c <= 0); break;
                case BinOp::GT: res = (c > 0); break;
                case BinOp::GE: res = (c >= 0); break;
                default: break;
            }
            return Value::Int(res ? 1 : 0);
        }
    }
    return Value::Null();
}

bool eval_pred(const Expr* e, const Tuple& row, const OutSchema& schema) {
    if (!e) return true;
    Value v = eval_expr(e, row, schema);
    return v.is_int() && v.as_int() != 0;
}

// ---- helpers --------------------------------------------------------------
static OutSchema make_schema(TableInfo* info, const std::string& alias) {
    OutSchema s;
    for (const auto& c : info->schema.columns())
        s.push_back(OutColumn{alias, c.name, c.type});
    return s;
}

// ---- SeqScan --------------------------------------------------------------
SeqScan::SeqScan(TableAccess ta, std::string alias) : ta_(ta) {
    out_schema_ = make_schema(ta.info, alias);
}
void SeqScan::open() {
    rows_.clear();
    pos_ = 0;
    Schema& schema = ta_.info->schema;
    ta_.heap->scan([&](const RID&, const std::vector<uint8_t>& bytes) {
        rows_.push_back(deserialize_tuple(schema, bytes));
    });
}
std::optional<Tuple> SeqScan::next() {
    if (pos_ >= rows_.size()) return std::nullopt;
    return rows_[pos_++];
}

// ---- IndexScan ------------------------------------------------------------
IndexScan::IndexScan(TableAccess ta, std::string alias,
                     std::optional<Value> low, std::optional<Value> high)
    : ta_(ta), low_(std::move(low)), high_(std::move(high)) {
    out_schema_ = make_schema(ta.info, alias);
}
void IndexScan::open() {
    rows_.clear();
    pos_ = 0;
    Schema& schema = ta_.info->schema;
    BPlusTree* idx = ta_.info->pk_index.get();
    idx->range_scan(low_, high_, [&](const Value&, const RID& rid) {
        auto bytes = ta_.heap->get(rid);
        if (bytes) rows_.push_back(deserialize_tuple(schema, *bytes));
    });
}
std::optional<Tuple> IndexScan::next() {
    if (pos_ >= rows_.size()) return std::nullopt;
    return rows_[pos_++];
}

// ---- Filter ---------------------------------------------------------------
Filter::Filter(OpPtr child, ExprPtr pred) : child_(std::move(child)), pred_(std::move(pred)) {
    out_schema_ = child_->schema();
}
std::optional<Tuple> Filter::next() {
    while (auto row = child_->next()) {
        if (eval_pred(pred_.get(), *row, out_schema_)) return row;
    }
    return std::nullopt;
}

// ---- NestedLoopJoin -------------------------------------------------------
NestedLoopJoin::NestedLoopJoin(OpPtr left, OpPtr right, ExprPtr pred)
    : left_(std::move(left)), right_(std::move(right)), pred_(std::move(pred)) {
    out_schema_ = left_->schema();
    for (auto& c : right_->schema()) out_schema_.push_back(c);
}
void NestedLoopJoin::open() {
    left_->open();
    right_->open();
    right_rows_.clear();
    while (auto r = right_->next()) right_rows_.push_back(*r);   // materialize inner
    cur_left_ = left_->next();
    right_pos_ = 0;
}
std::optional<Tuple> NestedLoopJoin::next() {
    while (cur_left_) {
        while (right_pos_ < right_rows_.size()) {
            Tuple combined = *cur_left_;
            const Tuple& rrow = right_rows_[right_pos_++];
            combined.insert(combined.end(), rrow.begin(), rrow.end());
            if (eval_pred(pred_.get(), combined, out_schema_)) return combined;
        }
        cur_left_ = left_->next();
        right_pos_ = 0;
    }
    return std::nullopt;
}

// ---- Projection -----------------------------------------------------------
Projection::Projection(OpPtr child, const std::vector<SelectItem>& items)
    : child_(std::move(child)) {
    const OutSchema& in = child_->schema();
    for (const auto& it : items) {
        if (it.is_star) {
            for (size_t i = 0; i < in.size(); ++i) {
                col_indexes_.push_back((int)i);
                out_schema_.push_back(in[i]);
            }
            continue;
        }
        int idx = resolve_column(in, it.table, it.column);
        if (idx < 0) throw std::runtime_error("unknown/ambiguous column in SELECT: " + it.column);
        col_indexes_.push_back(idx);
        OutColumn oc = in[idx];
        oc.name = it.alias.empty() ? oc.name : it.alias;
        out_schema_.push_back(oc);
    }
}
std::optional<Tuple> Projection::next() {
    auto row = child_->next();
    if (!row) return std::nullopt;
    Tuple out;
    out.reserve(col_indexes_.size());
    for (int idx : col_indexes_) out.push_back((*row)[idx]);
    return out;
}

// ---- Aggregate ------------------------------------------------------------
Aggregate::Aggregate(OpPtr child, std::vector<int> group_cols,
                     std::vector<AggOutput> outputs)
    : child_(std::move(child)), group_cols_(std::move(group_cols)),
      outputs_(std::move(outputs)) {
    for (auto& o : outputs_) out_schema_.push_back(OutColumn{"", o.name, o.type});
}
void Aggregate::open() {
    child_->open();
    output_.clear();
    pos_ = 0;

    // Per group, per aggregate-output accumulator state.
    struct Acc {
        Tuple key;
        int64_t count = 0;
        std::vector<int64_t> sum;
        std::vector<Value>   mn, mx;
        std::vector<bool>    seen;
    };
    std::vector<Acc> groups;
    std::vector<std::string> group_keys;
    auto key_str = [](const Tuple& t) {
        std::string s;
        for (auto& v : t) { s += v.to_string(); s.push_back('\x01'); }
        return s;
    };

    while (auto row = child_->next()) {
        Tuple gk;
        for (int gc : group_cols_) gk.push_back((*row)[gc]);
        std::string ks = key_str(gk);
        int gi = -1;
        for (size_t i = 0; i < group_keys.size(); ++i)
            if (group_keys[i] == ks) { gi = (int)i; break; }
        if (gi == -1) {
            Acc a; a.key = gk;
            a.sum.assign(outputs_.size(), 0);
            a.mn.assign(outputs_.size(), Value::Null());
            a.mx.assign(outputs_.size(), Value::Null());
            a.seen.assign(outputs_.size(), false);
            groups.push_back(std::move(a));
            group_keys.push_back(ks);
            gi = (int)groups.size() - 1;
        }
        Acc& a = groups[gi];
        a.count++;
        for (size_t j = 0; j < outputs_.size(); ++j) {
            const AggOutput& o = outputs_[j];
            if (o.is_group || o.star || o.col_index < 0) continue;
            const Value& v = (*row)[o.col_index];
            if (v.is_null()) continue;
            if (v.is_int()) a.sum[j] += v.as_int();
            if (!a.seen[j]) { a.mn[j] = v; a.mx[j] = v; a.seen[j] = true; }
            else { if (v.compare(a.mn[j]) < 0) a.mn[j] = v;
                   if (v.compare(a.mx[j]) > 0) a.mx[j] = v; }
        }
    }

    if (groups.empty() && group_cols_.empty()) {     // COUNT(*) over empty input
        Acc a; a.sum.assign(outputs_.size(), 0);
        a.mn.assign(outputs_.size(), Value::Null());
        a.mx.assign(outputs_.size(), Value::Null());
        a.seen.assign(outputs_.size(), false);
        groups.push_back(std::move(a));
    }

    for (auto& a : groups) {
        Tuple out;
        for (size_t j = 0; j < outputs_.size(); ++j) {
            const AggOutput& o = outputs_[j];
            if (o.is_group) { out.push_back(a.key[o.group_index]); continue; }
            switch (o.func) {
                case AggFunc::COUNT: out.push_back(Value::Int(a.count)); break;
                case AggFunc::SUM:   out.push_back(Value::Int(a.sum[j])); break;
                case AggFunc::AVG:   out.push_back(Value::Int(a.count ? a.sum[j] / a.count : 0)); break;
                case AggFunc::MIN:   out.push_back(a.seen[j] ? a.mn[j] : Value::Null()); break;
                case AggFunc::MAX:   out.push_back(a.seen[j] ? a.mx[j] : Value::Null()); break;
                default:             out.push_back(Value::Null()); break;
            }
        }
        output_.push_back(std::move(out));
    }
}
std::optional<Tuple> Aggregate::next() {
    if (pos_ >= output_.size()) return std::nullopt;
    return output_[pos_++];
}

// ---- Sort -----------------------------------------------------------------
Sort::Sort(OpPtr child, int col, bool desc)
    : child_(std::move(child)), col_(col), desc_(desc) {
    out_schema_ = child_->schema();
}
void Sort::open() {
    child_->open();
    rows_.clear();
    pos_ = 0;
    while (auto r = child_->next()) rows_.push_back(*r);
    std::stable_sort(rows_.begin(), rows_.end(), [&](const Tuple& a, const Tuple& b) {
        int c = a[col_].compare(b[col_]);
        return desc_ ? (c > 0) : (c < 0);
    });
}
std::optional<Tuple> Sort::next() {
    if (pos_ >= rows_.size()) return std::nullopt;
    return rows_[pos_++];
}

}  // namespace minidb
