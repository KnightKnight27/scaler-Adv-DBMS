#include "execution/operators.h"

#include <string>

#include "catalog/record.h"
#include "common/exception.h"
#include "execution/expr_eval.h"

namespace minidb {

namespace {

// A type-tagged string key for a Value, used for hash-join buckets and grouping.
std::string value_key(const Value& v) {
    if (std::holds_alternative<std::int64_t>(v)) return "i" + std::to_string(std::get<std::int64_t>(v));
    if (std::holds_alternative<double>(v))       return "d" + std::to_string(std::get<double>(v));
    return "s" + std::get<std::string>(v);
}

bool is_number(const Value& v) {
    return std::holds_alternative<std::int64_t>(v) || std::holds_alternative<double>(v);
}
double as_number(const Value& v) {
    return std::holds_alternative<std::int64_t>(v)
               ? static_cast<double>(std::get<std::int64_t>(v))
               : std::get<double>(v);
}
// strict-less for MIN/MAX
bool value_less(const Value& a, const Value& b) {
    if (is_number(a) && is_number(b)) return as_number(a) < as_number(b);
    if (std::holds_alternative<std::string>(a) && std::holds_alternative<std::string>(b))
        return std::get<std::string>(a) < std::get<std::string>(b);
    throw DBException("aggregate: cannot order values of different types");
}

OutSchema schema_to_out(const Schema& schema, const std::string& alias) {
    OutSchema out;
    for (const auto& c : schema.columns()) out.push_back(OutColumn{alias, c.name, c.type});
    return out;
}

} // namespace

// ---------------- SeqScan ----------------
SeqScan::SeqScan(StorageEngine* engine, std::string table, Schema schema, std::string alias)
    : engine_(engine), table_(std::move(table)), schema_(std::move(schema)) {
    out_schema_ = schema_to_out(schema_, alias.empty() ? table_ : alias);
}
void SeqScan::open() { cursor_ = engine_->scan(table_); }
bool SeqScan::next(Tuple& out) {
    std::int64_t key; std::string row;
    if (!cursor_->next(key, row)) return false;
    out.values = Record::deserialize(schema_, row);
    return true;
}

// ---------------- IndexScan ----------------
IndexScan::IndexScan(StorageEngine* engine, std::string table, Schema schema, std::string alias,
                     std::int64_t lo, std::int64_t hi)
    : engine_(engine), table_(std::move(table)), schema_(std::move(schema)), lo_(lo), hi_(hi) {
    out_schema_ = schema_to_out(schema_, alias.empty() ? table_ : alias);
}
void IndexScan::open() { cursor_ = engine_->range(table_, lo_, hi_); }
bool IndexScan::next(Tuple& out) {
    std::int64_t key; std::string row;
    if (!cursor_->next(key, row)) return false;
    out.values = Record::deserialize(schema_, row);
    return true;
}

// ---------------- Filter ----------------
bool Filter::next(Tuple& out) {
    while (child_->next(out)) {
        if (eval_predicate(predicate_, out, child_->out_schema())) return true;
    }
    return false;
}

// ---------------- Project ----------------
Project::Project(std::unique_ptr<Operator> child, std::vector<int> indices, OutSchema out)
    : child_(std::move(child)), indices_(std::move(indices)), out_schema_(std::move(out)) {}
bool Project::next(Tuple& out) {
    Tuple in;
    if (!child_->next(in)) return false;
    out.values.clear();
    out.values.reserve(indices_.size());
    for (int idx : indices_) out.values.push_back(in.values[static_cast<std::size_t>(idx)]);
    return true;
}

// ---------------- NestedLoopJoin ----------------
NestedLoopJoin::NestedLoopJoin(std::unique_ptr<Operator> left, std::unique_ptr<Operator> right,
                               const Expr* on)
    : left_(std::move(left)), right_(std::move(right)), on_(on) {
    out_schema_ = left_->out_schema();
    const OutSchema& rs = right_->out_schema();
    out_schema_.insert(out_schema_.end(), rs.begin(), rs.end());
}
void NestedLoopJoin::open() {
    left_->open();
    right_->open();
    Tuple t;
    while (right_->next(t)) right_rows_.push_back(t);
    have_left_ = false;
    right_idx_ = 0;
}
bool NestedLoopJoin::next(Tuple& out) {
    for (;;) {
        if (!have_left_) {
            if (!left_->next(cur_left_)) return false;
            have_left_ = true;
            right_idx_ = 0;
        }
        while (right_idx_ < right_rows_.size()) {
            Tuple combined;
            combined.values = cur_left_.values;
            const auto& rv = right_rows_[right_idx_++].values;
            combined.values.insert(combined.values.end(), rv.begin(), rv.end());
            if (!on_ || eval_predicate(on_, combined, out_schema_)) { out = std::move(combined); return true; }
        }
        have_left_ = false;  // advance to the next left tuple
    }
}
void NestedLoopJoin::close() { left_->close(); right_->close(); }

// ---------------- HashJoin ----------------
HashJoin::HashJoin(std::unique_ptr<Operator> left, std::unique_ptr<Operator> right,
                   int left_key, int right_key, bool build_on_left)
    : left_(std::move(left)), right_(std::move(right)),
      left_key_(left_key), right_key_(right_key), build_on_left_(build_on_left) {
    out_schema_ = left_->out_schema();
    const OutSchema& rs = right_->out_schema();
    out_schema_.insert(out_schema_.end(), rs.begin(), rs.end());
}
void HashJoin::open() {
    left_->open();
    right_->open();
    // Build the hash table on the chosen (smaller) side.
    Operator* build_op   = build_on_left_ ? left_.get() : right_.get();
    int       build_key  = build_on_left_ ? left_key_ : right_key_;
    Tuple t;
    while (build_op->next(t)) {
        std::string k = value_key(t.values[static_cast<std::size_t>(build_key)]);
        build_[k].push_back(t);
    }
    have_probe_ = false;
}
bool HashJoin::next(Tuple& out) {
    Operator* probe_op  = build_on_left_ ? right_.get() : left_.get();
    int       probe_key = build_on_left_ ? right_key_ : left_key_;
    for (;;) {
        if (!have_probe_) {
            if (!probe_op->next(cur_probe_)) return false;
            have_probe_ = true;
            std::string k = value_key(cur_probe_.values[static_cast<std::size_t>(probe_key)]);
            auto it = build_.find(k);
            bucket_ = (it == build_.end()) ? nullptr : &it->second;
            bucket_idx_ = 0;
        }
        if (bucket_ && bucket_idx_ < bucket_->size()) {
            // Emit in left++right order regardless of which side was built.
            const Tuple& build_tuple = (*bucket_)[bucket_idx_++];
            const Tuple& left_t  = build_on_left_ ? build_tuple : cur_probe_;
            const Tuple& right_t = build_on_left_ ? cur_probe_  : build_tuple;
            Tuple combined;
            combined.values = left_t.values;
            combined.values.insert(combined.values.end(), right_t.values.begin(), right_t.values.end());
            out = std::move(combined);
            return true;
        }
        have_probe_ = false;
    }
}
void HashJoin::close() { left_->close(); right_->close(); }

// ---------------- Aggregate ----------------
Aggregate::Aggregate(std::unique_ptr<Operator> child, std::vector<int> group_by,
                     std::vector<AggSpec> aggs, OutSchema out)
    : child_(std::move(child)), group_by_(std::move(group_by)),
      aggs_(std::move(aggs)), out_schema_(std::move(out)) {}

void Aggregate::open() {
    child_->open();

    struct Acc { std::int64_t count = 0; double sum = 0; Value mn; Value mx; bool has = false; };
    struct Group { std::vector<Value> keys; std::vector<Acc> accs; };

    std::unordered_map<std::string, Group> groups;
    std::vector<std::string> order;  // stable output order

    auto ensure_group = [&](const std::string& gk, const std::vector<Value>& keys) -> Group& {
        auto it = groups.find(gk);
        if (it == groups.end()) {
            Group g;
            g.keys = keys;
            g.accs.resize(aggs_.size());
            order.push_back(gk);
            it = groups.emplace(gk, std::move(g)).first;
        }
        return it->second;
    };

    // No GROUP BY => a single group over the whole input (even if empty).
    if (group_by_.empty()) ensure_group("", {});

    Tuple t;
    while (child_->next(t)) {
        std::vector<Value> keys;
        std::string gk;
        for (int gi : group_by_) {
            keys.push_back(t.values[static_cast<std::size_t>(gi)]);
            gk += value_key(t.values[static_cast<std::size_t>(gi)]) + "|";
        }
        Group& g = ensure_group(gk, keys);
        for (std::size_t a = 0; a < aggs_.size(); ++a) {
            Acc& acc = g.accs[a];
            const AggSpec& spec = aggs_[a];
            if (spec.func == "COUNT") { ++acc.count; continue; }
            const Value& v = t.values[static_cast<std::size_t>(spec.col)];
            ++acc.count;
            if (is_number(v)) acc.sum += as_number(v);
            if (!acc.has) { acc.mn = acc.mx = v; acc.has = true; }
            else { if (value_less(v, acc.mn)) acc.mn = v; if (value_less(acc.mx, v)) acc.mx = v; }
        }
    }

    for (const std::string& gk : order) {
        Group& g = groups[gk];
        Tuple row;
        for (const Value& k : g.keys) row.values.push_back(k);
        for (std::size_t a = 0; a < aggs_.size(); ++a) {
            const AggSpec& spec = aggs_[a];
            const Acc& acc = g.accs[a];
            if (spec.func == "COUNT")      row.values.push_back(Value{acc.count});
            else if (spec.func == "SUM")   row.values.push_back(Value{acc.sum});
            else if (spec.func == "AVG")   row.values.push_back(Value{acc.count ? acc.sum / acc.count : 0.0});
            else if (spec.func == "MIN")   row.values.push_back(acc.has ? acc.mn : Value{std::int64_t{0}});
            else if (spec.func == "MAX")   row.values.push_back(acc.has ? acc.mx : Value{std::int64_t{0}});
            else throw DBException("aggregate: unknown function " + spec.func);
        }
        results_.push_back(std::move(row));
    }
}
bool Aggregate::next(Tuple& out) {
    if (pos_ >= results_.size()) return false;
    out = results_[pos_++];
    return true;
}

} // namespace minidb
