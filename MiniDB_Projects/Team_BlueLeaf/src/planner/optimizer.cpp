#include "planner/optimizer.h"

#include <algorithm>
#include <limits>

#include "common/exception.h"
#include "execution/expr_eval.h"
#include "planner/statistics.h"

namespace minidb {

namespace {

// Split "table.col" / "col" into (table, name).
std::pair<std::string, std::string> split_column(const std::string& s) {
    auto dot = s.find('.');
    if (dot == std::string::npos) return {"", s};
    return {s.substr(0, dot), s.substr(dot + 1)};
}

// Non-throwing column resolution; -1 if absent/ambiguous-miss.
int resolve_opt(const OutSchema& schema, const std::string& table, const std::string& name) {
    int found = -1;
    for (std::size_t i = 0; i < schema.size(); ++i) {
        if (schema[i].name != name) continue;
        if (!table.empty() && schema[i].table != table) continue;
        if (found != -1) return -1;
        found = static_cast<int>(i);
    }
    return found;
}

// Conjuncts joined by top-level AND (does not descend into OR).
void collect_conjuncts(const Expr* e, std::vector<const BinaryExpr*>& out) {
    if (!e || e->kind() != ExprKind::Binary) return;
    const auto* b = static_cast<const BinaryExpr*>(e);
    if (b->op == "AND") { collect_conjuncts(b->left.get(), out); collect_conjuncts(b->right.get(), out); }
    else out.push_back(b);
}

bool is_pk_column(const Expr* e, const std::string& alias, const std::string& pk) {
    if (e->kind() != ExprKind::Column) return false;
    const auto* c = static_cast<const ColumnExpr*>(e);
    return c->name == pk && (c->table.empty() || c->table == alias);
}

struct PkRange {
    bool         use = false;
    std::int64_t lo  = std::numeric_limits<std::int64_t>::min();
    std::int64_t hi  = std::numeric_limits<std::int64_t>::max();
};

PkRange extract_pk_range(const Expr* where, const std::string& alias, const std::string& pk) {
    PkRange r;
    std::vector<const BinaryExpr*> conj;
    collect_conjuncts(where, conj);
    for (const BinaryExpr* c : conj) {
        std::string op = c->op;
        const Expr* col = nullptr;
        const Expr* lit = nullptr;
        if (is_pk_column(c->left.get(), alias, pk)) { col = c->left.get(); lit = c->right.get(); }
        else if (is_pk_column(c->right.get(), alias, pk)) {
            col = c->right.get(); lit = c->left.get();
            if (op == "<")  op = ">";  else if (op == ">")  op = "<";    // flip sides
            else if (op == "<=") op = ">="; else if (op == ">=") op = "<=";
        }
        if (!col || lit->kind() != ExprKind::Literal) continue;
        const Value& v = static_cast<const LiteralExpr*>(lit)->value;
        if (!std::holds_alternative<std::int64_t>(v)) continue;
        std::int64_t x = std::get<std::int64_t>(v);
        if (op == "=")  { r.lo = std::max(r.lo, x); r.hi = std::min(r.hi, x); r.use = true; }
        else if (op == ">")  { r.lo = std::max(r.lo, x + 1); r.use = true; }
        else if (op == ">=") { r.lo = std::max(r.lo, x);     r.use = true; }
        else if (op == "<")  { r.hi = std::min(r.hi, x - 1); r.use = true; }
        else if (op == "<=") { r.hi = std::min(r.hi, x);     r.use = true; }
    }
    if (r.use && r.lo > r.hi) r.use = false;
    return r;
}

double estimate_rows(const TableStats& s, std::int64_t lo, std::int64_t hi) {
    if (s.empty || s.row_count == 0) return 0;
    double total = static_cast<double>(s.key_max - s.key_min) + 1.0;
    if (total < 1) total = 1;
    std::int64_t a = std::max(lo, s.key_min), b = std::min(hi, s.key_max);
    double matched = static_cast<double>(b - a) + 1.0;
    if (matched < 0) matched = 0;
    return static_cast<double>(s.row_count) * (matched / total);
}

} // namespace

std::unique_ptr<Operator> Optimizer::build_access(const std::string& table, const std::string& alias,
                                                  const Expr* where, bool allow_index) {
    TableInfo* t = cat_->get_table(table);
    if (!t) throw DBException("Optimizer: no such table: " + table);
    const Schema& schema = t->schema;
    std::string a = alias.empty() ? table : alias;
    std::string pk = (t->pk_col >= 0) ? schema.column(t->pk_col).name : "";

    std::unique_ptr<Operator> scan;
    if (allow_index && where && !pk.empty()) {
        PkRange r = extract_pk_range(where, a, pk);
        if (r.use) {
            TableStats st = gather_stats(engine_, table);
            bool selective = (r.lo == r.hi) ||
                             estimate_rows(st, r.lo, r.hi) <= 0.3 * static_cast<double>(st.row_count);
            if (selective) {
                explain_ += "IndexScan(" + table + " pk in [" + std::to_string(r.lo) + "," +
                            std::to_string(r.hi) + "]) ";
                scan = std::make_unique<IndexScan>(engine_, table, schema, a, r.lo, r.hi);
            }
        }
    }
    if (!scan) {
        explain_ += "SeqScan(" + table + ") ";
        scan = std::make_unique<SeqScan>(engine_, table, schema, a);
    }
    std::unique_ptr<Operator> op = std::move(scan);
    if (where) { explain_ += "+Filter "; op = std::make_unique<Filter>(std::move(op), where); }
    return op;
}

std::unique_ptr<Operator> Optimizer::build_join(const SelectStmt& s) {
    auto left  = build_access(s.from_table, s.from_alias, nullptr, false);
    auto right = build_access(s.join_table, s.join_alias, nullptr, false);

    const Expr* on = s.join_on.get();
    bool equi = on && on->kind() == ExprKind::Binary &&
                static_cast<const BinaryExpr*>(on)->op == "=" &&
                static_cast<const BinaryExpr*>(on)->left->kind() == ExprKind::Column &&
                static_cast<const BinaryExpr*>(on)->right->kind() == ExprKind::Column;

    if (equi) {
        const auto* b  = static_cast<const BinaryExpr*>(on);
        const auto* c1 = static_cast<const ColumnExpr*>(b->left.get());
        const auto* c2 = static_cast<const ColumnExpr*>(b->right.get());
        int lk = resolve_opt(left->out_schema(), c1->table, c1->name);
        int rk = resolve_opt(right->out_schema(), c2->table, c2->name);
        if (lk < 0 || rk < 0) {  // operands written in the other order
            lk = resolve_opt(left->out_schema(), c2->table, c2->name);
            rk = resolve_opt(right->out_schema(), c1->table, c1->name);
        }
        if (lk < 0 || rk < 0) throw DBException("Optimizer: cannot resolve join keys");

        TableStats sl = gather_stats(engine_, s.from_table);
        TableStats sr = gather_stats(engine_, s.join_table);
        bool build_on_left = sl.row_count <= sr.row_count;  // build the smaller side
        explain_ += "HashJoin(build on " + (build_on_left ? s.from_table : s.join_table) + ") ";
        return std::make_unique<HashJoin>(std::move(left), std::move(right), lk, rk, build_on_left);
    }

    explain_ += "NestedLoopJoin ";
    return std::make_unique<NestedLoopJoin>(std::move(left), std::move(right), on);
}

std::unique_ptr<Operator> Optimizer::build_aggregate(std::unique_ptr<Operator> child,
                                                     const SelectStmt& s) {
    const OutSchema& cs = child->out_schema();
    std::vector<int> gb;
    OutSchema out;
    for (const std::string& g : s.group_by) {
        auto [tbl, nm] = split_column(g);
        int idx = resolve_opt(cs, tbl, nm);
        if (idx < 0) throw DBException("Optimizer: unknown GROUP BY column " + g);
        gb.push_back(idx);
        out.push_back(cs[static_cast<std::size_t>(idx)]);
    }
    std::vector<AggSpec> specs;
    for (const AggCall& a : s.aggregates) {
        AggSpec spec;
        spec.func = a.func;
        if (a.column == "*") { spec.col = -1; }
        else {
            auto [tbl, nm] = split_column(a.column);
            spec.col = resolve_opt(cs, tbl, nm);
            if (spec.col < 0) throw DBException("Optimizer: unknown aggregate column " + a.column);
        }
        ValueType vt = (a.func == "COUNT") ? ValueType::INT
                     : (a.func == "SUM" || a.func == "AVG") ? ValueType::DOUBLE
                     : (spec.col >= 0 ? cs[static_cast<std::size_t>(spec.col)].type : ValueType::INT);
        out.push_back(OutColumn{"", a.func + "(" + a.column + ")", vt});
        specs.push_back(spec);
    }
    explain_ += "+Aggregate ";
    return std::make_unique<Aggregate>(std::move(child), std::move(gb), std::move(specs), std::move(out));
}

std::unique_ptr<Operator> Optimizer::build_project(std::unique_ptr<Operator> child,
                                                   const std::vector<std::string>& columns) {
    const OutSchema& cs = child->out_schema();
    std::vector<int> idxs;
    OutSchema out;
    for (const std::string& c : columns) {
        auto [tbl, nm] = split_column(c);
        int idx = resolve_opt(cs, tbl, nm);
        if (idx < 0) throw DBException("Optimizer: unknown column " + c);
        idxs.push_back(idx);
        out.push_back(cs[static_cast<std::size_t>(idx)]);
    }
    explain_ += "+Project ";
    return std::make_unique<Project>(std::move(child), std::move(idxs), std::move(out));
}

std::unique_ptr<Operator> Optimizer::plan(const SelectStmt& s) {
    explain_.clear();
    std::unique_ptr<Operator> op;

    if (s.join_table.empty()) {
        op = build_access(s.from_table, s.from_alias, s.where.get(), /*allow_index=*/true);
    } else {
        op = build_join(s);
        if (s.where) { explain_ += "+Filter(where) "; op = std::make_unique<Filter>(std::move(op), s.where.get()); }
    }

    if (!s.aggregates.empty() || !s.group_by.empty()) {
        op = build_aggregate(std::move(op), s);
    } else if (!(s.columns.size() == 1 && s.columns[0] == "*")) {
        op = build_project(std::move(op), s.columns);
    }
    return op;
}

} // namespace minidb
