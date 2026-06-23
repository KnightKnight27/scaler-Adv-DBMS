#include "optimizer.h"

#include <cmath>

#include "../execution/executors.h"  // CoerceTo
#include "../index/bplus_tree.h"

namespace minidb {

namespace {
// Find an equality "indexed_col = const" usable for an index point lookup.
// Only descends through AND (an equality under OR cannot drive a single lookup).
bool FindEquality(const Expr* e, TableInfo* t, int* col_out, Value* key_out) {
    if (!e || e->type != ExprType::Binary) return false;
    if (e->op == "AND") {
        return FindEquality(e->left.get(), t, col_out, key_out) ||
               FindEquality(e->right.get(), t, col_out, key_out);
    }
    if (e->op == "=") {
        const Expr *col = nullptr, *lit = nullptr;
        if (e->left->type == ExprType::Column && e->right->type == ExprType::Const) {
            col = e->left.get(); lit = e->right.get();
        } else if (e->right->type == ExprType::Column && e->left->type == ExprType::Const) {
            col = e->right.get(); lit = e->left.get();
        }
        if (!col) return false;
        int idx = t->schema.GetColIdx(col->col_name);
        if (idx < 0 || !t->FindIndexOn(idx)) return false;
        *col_out = idx;
        *key_out = lit->val;
        return true;
    }
    return false;
}
}  // namespace

ScanPlan Optimizer::ChooseScan(TableInfo* t, const Expr* where) {
    ScanPlan p;
    double rows = static_cast<double>(t->num_rows > 0 ? t->num_rows : 1);
    p.seq_cost = rows;  // a sequential scan touches every row

    int col = -1;
    Value key;
    if (where && FindEquality(where, t, &col, &key)) {
        IndexInfo* ix = t->FindIndexOn(col);
        p.selectivity = ix->unique ? (1.0 / rows) : kDefaultEqSelectivity;
        double est_match = std::max(1.0, rows * p.selectivity);
        double height = static_cast<double>(ix->tree->Height());
        p.index_cost = height + est_match;  // tree descent + matched rows fetched
        if (p.index_cost < p.seq_cost) {
            p.use_index = true;
            p.index = ix;
            p.key = CoerceTo(key, t->schema.GetColumn(col).type);
            p.desc = "IndexScan on " + ix->column + " (est rows " +
                     std::to_string(static_cast<long long>(est_match)) + ", cost " +
                     std::to_string(static_cast<long long>(p.index_cost)) + " < seq cost " +
                     std::to_string(static_cast<long long>(p.seq_cost)) + ")";
            return p;
        }
    }
    p.desc = "SeqScan (cost " + std::to_string(static_cast<long long>(p.seq_cost)) + ")";
    return p;
}

}  // namespace minidb
