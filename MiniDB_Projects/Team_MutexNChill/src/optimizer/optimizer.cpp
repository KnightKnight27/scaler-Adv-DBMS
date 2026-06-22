#include "optimizer.h"
#include "../query/expressions.h"
#include <algorithm>
#include <cmath>

Optimizer::Optimizer(Catalog& catalog) : cat(catalog) {}

// Estimate the fraction of rows that will pass this expression (0.0 to 1.0).
// We only look at predicates on the indexed "id" column for the index-scan decision.
double Optimizer::estimateSelectivity(Expr* where, TableInfo* t) {
    if (!where) return 1.0; // no filter → all rows pass

    if (auto* cmp = dynamic_cast<CompareExpr*>(where)) {
        // Check if this is a predicate on "id"
        auto* col = dynamic_cast<ColumnExpr*>(cmp->left);
        auto* num = dynamic_cast<NumberExpr*>(cmp->right);
        if (!col || !num) return 0.5; // unknown, assume half

        if (col->column != "id") return 0.5; // not on id column

        int lo = t->id_stats.min_val;
        int hi = t->id_stats.max_val;
        int range = (hi > lo) ? (hi - lo) : 1;
        int v = num->value;

        if (cmp->op == "=") {
            // Equality: 1/number_of_distinct_values
            return 1.0 / std::max(1, t->id_stats.distinct);
        }
        if (cmp->op == ">") return (double)(hi - v) / range;
        if (cmp->op == "<") return (double)(v - lo) / range;
        if (cmp->op == ">=") return (double)(hi - v + 1) / range;
        if (cmp->op == "<=") return (double)(v - lo + 1) / range;
    }

    if (auto* logic = dynamic_cast<LogicExpr*>(where)) {
        double l = estimateSelectivity(logic->left,  t);
        double r = estimateSelectivity(logic->right, t);
        if (logic->op == "AND") return l * r;
        if (logic->op == "OR")  return l + r - l * r;
    }

    return 0.5;
}

// Check if the WHERE clause is an equality predicate on "id".
static bool isIdEquality(Expr* where) {
    if (!where) return false;
    auto* cmp = dynamic_cast<CompareExpr*>(where);
    if (!cmp) return false;
    auto* col = dynamic_cast<ColumnExpr*>(cmp->left);
    return col && col->column == "id" && cmp->op == "=";
}

QueryPlan Optimizer::plan(SelectStmt* stmt) {
    QueryPlan p;
    p.table = stmt->table;

    TableInfo* t = cat.getTable(stmt->table);
    if (!t) {
        p.scan   = ScanType::SEQ_SCAN;
        p.reason = "Table not found — defaulting to SeqScan";
        return p;
    }

    // ----- Scan choice -----
    // Rule 1: if no rows in table, use SeqScan (nothing to index)
    if (t->row_count == 0) {
        p.scan   = ScanType::SEQ_SCAN;
        p.reason = "Empty table — using SeqScan";
        return p;
    }

    double sel = estimateSelectivity(stmt->where, t);

    // Rule 2: equality on id → always use index scan (most selective possible)
    if (isIdEquality(stmt->where)) {
        p.scan   = ScanType::INDEX_SCAN;
        p.reason = "Equality predicate on indexed column id (selectivity ~1/" +
                   std::to_string(std::max(1, t->id_stats.distinct)) + ") → IndexScan";
        return p;
    }

    // Rule 3: if estimated selectivity < 20%, index scan is cheaper
    // (we'd read < 20% of rows so random I/O is worth it)
    if (sel < 0.20) {
        p.scan   = ScanType::INDEX_SCAN;
        p.reason = "Low selectivity (~" + std::to_string((int)(sel*100)) +
                   "%) → IndexScan";
        return p;
    }

    // Otherwise: sequential scan (cheaper when reading most of the table)
    p.scan   = ScanType::SEQ_SCAN;
    p.reason = "Selectivity ~" + std::to_string((int)(sel*100)) +
               "%, " + std::to_string(t->row_count) + " rows → SeqScan";

    // ----- Join order -----
    if (!stmt->join_table.empty()) {
        TableInfo* inner = cat.getTable(stmt->join_table);
        int outer_rows = t->row_count;
        int inner_rows = inner ? inner->row_count : 0;

        // Put the smaller table as the outer (driving) loop to minimise iterations.
        if (inner_rows < outer_rows) {
            p.join_outer = stmt->join_table;
            p.join_inner = stmt->table;
            p.reason += " | Join: " + stmt->join_table + " as outer (" +
                        std::to_string(inner_rows) + " rows) × " +
                        stmt->table + " (" + std::to_string(outer_rows) + " rows)";
        } else {
            p.join_outer = stmt->table;
            p.join_inner = stmt->join_table;
            p.reason += " | Join: " + stmt->table + " as outer (" +
                        std::to_string(outer_rows) + " rows) × " +
                        stmt->join_table + " (" + std::to_string(inner_rows) + " rows)";
        }
    }

    return p;
}
