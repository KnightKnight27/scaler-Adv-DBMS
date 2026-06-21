#include "optimizer/optimizer.h"
#include <algorithm>

namespace minidb {

// Does predicate `p` reference only `table` (and so can be pushed down as a
// single-table filter)? A constant comparison on a column of `table` qualifies.
static bool predicate_for_table(const Predicate &p, const std::string &table,
                                const TableInfo *ti) {
    auto col_in_table = [&](const Operand &o) {
        if (!o.is_column) return false;
        if (!o.col.table.empty()) return o.col.table == table;
        return ti->schema.index_of(o.col.column) >= 0;
    };
    // form: column <op> constant
    if (col_in_table(p.left) && !p.right.is_column) return true;
    if (col_in_table(p.right) && !p.left.is_column) return true;
    return false;
}

double Optimizer::selectivity(const Predicate &p, const TableInfo *ti) {
    // Classic textbook estimates. For equality on a key column we use 1/N
    // (assuming uniform distinct values); ranges get a default fraction.
    double n = std::max<double>(1.0, (double)ti->row_count);
    switch (p.op) {
        case CompOp::EQ: return 1.0 / n;          // unique-ish match
        case CompOp::NE: return 1.0 - 1.0 / n;
        case CompOp::LT:
        case CompOp::LE:
        case CompOp::GT:
        case CompOp::GE: return 1.0 / 3.0;        // default range selectivity
    }
    return 1.0 / 3.0;
}

TablePlan Optimizer::plan_table(const std::string &table, const std::vector<Predicate> &preds) {
    TablePlan plan;
    plan.table = table;
    TableInfo *ti = catalog_.get_table(table);
    double n = ti ? (double)ti->row_count : 0.0;

    // Gather predicates that apply to this table.
    std::vector<Predicate> mine;
    for (const auto &p : preds)
        if (ti && predicate_for_table(p, table, ti)) mine.push_back(p);

    // Look for an index-usable predicate on the primary-key column.
    int pk = ti ? ti->schema.pk_index : -1;
    auto is_pk_col = [&](const Operand &o) {
        if (!o.is_column || pk < 0) return false;
        int idx = (!o.col.table.empty() || true) ? ti->schema.index_of(o.col.column) : -1;
        return idx == pk;
    };

    plan.method = AccessMethod::SEQ_SCAN;
    plan.est_rows = n;
    plan.est_cost = n; // sequential scan touches every row

    if (ti && ti->has_index) {
        for (const auto &p : mine) {
            // Normalise so the column is on the left.
            const Operand &colside = p.left.is_column ? p.left : p.right;
            const Operand &valside = p.left.is_column ? p.right : p.left;
            if (!is_pk_col(colside) || valside.is_column) continue;
            int32_t v = valside.constant.as_int();
            if (p.op == CompOp::EQ) {
                plan.method = AccessMethod::INDEX_POINT;
                plan.point_key = v;
                plan.est_rows = 1;
                plan.est_cost = 1.0 + 3.0; // ~tree height, far cheaper than full scan
                break;
            }
        }
    }

    // Residual filters = all single-table predicates except one consumed by an
    // index point lookup (kept simple: we still re-check it, which is harmless).
    plan.filters = mine;
    if (plan.method == AccessMethod::SEQ_SCAN) {
        double sel = 1.0;
        for (const auto &p : mine) sel *= selectivity(p, ti);
        plan.est_rows = n * sel;
    }
    return plan;
}

QueryPlan Optimizer::plan_select(const SelectStmt &stmt) {
    QueryPlan plan;

    if (!stmt.join.present) {
        plan.outer = plan_table(stmt.from_table, stmt.where);
        plan.has_join = false;
        TableInfo *ti = catalog_.get_table(stmt.from_table);
        plan.explanation = "SCAN " + stmt.from_table + " via " +
            (plan.outer.method == AccessMethod::SEQ_SCAN ? "SEQ_SCAN" :
             plan.outer.method == AccessMethod::INDEX_POINT ? "INDEX_POINT" : "INDEX_RANGE") +
            "  (est_rows=" + std::to_string((long)plan.outer.est_rows) +
            ", table_rows=" + std::to_string(ti ? ti->row_count : 0) + ")";
        return plan;
    }

    // Two-table join. Plan each side, then order so the smaller estimated
    // relation is the outer (drives the nested loop) -> fewer inner rescans.
    plan.has_join = true;
    TablePlan a = plan_table(stmt.from_table, stmt.where);
    TablePlan b = plan_table(stmt.join.table, stmt.where);
    if (a.est_rows <= b.est_rows) { plan.outer = a; plan.inner = b; }
    else                          { plan.outer = b; plan.inner = a; }

    // Normalise the join predicate to outer.col = inner.col.
    Predicate jp;
    jp.op = CompOp::EQ;
    jp.left.is_column = true;  jp.left.col  = stmt.join.left;
    jp.right.is_column = true; jp.right.col = stmt.join.right;
    plan.join_pred = jp;

    plan.explanation =
        "NESTED_LOOP_JOIN  outer=" + plan.outer.table +
        " (" + (plan.outer.method == AccessMethod::SEQ_SCAN ? "SEQ" : "INDEX") +
        ", est_rows=" + std::to_string((long)plan.outer.est_rows) + ")" +
        "  inner=" + plan.inner.table +
        " (" + (plan.inner.method == AccessMethod::SEQ_SCAN ? "SEQ" : "INDEX") +
        ", est_rows=" + std::to_string((long)plan.inner.est_rows) + ")";
    return plan;
}

} // namespace minidb
