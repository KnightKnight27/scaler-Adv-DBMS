#include "optimizer/Optimizer.hpp"

#include <algorithm>

using namespace std;

namespace minidb {

Optimizer::Optimizer(Catalog& catalog) : catalog_(catalog) {}

double Optimizer::estimateSelectivity(const TableDef& table, const WhereClause& where) {
    (void)table;
    if (where.op == CompareOp::EQ) return 0.1;
    if (where.op == CompareOp::LT || where.op == CompareOp::GT) return 0.33;
    return 0.5;
}

ScanType Optimizer::chooseScan(const TableDef& table, const WhereClause& where, bool has_where) {
    if (!has_where) return ScanType::TABLE_SCAN;
    if (where.column == table.primary_key_column && where.op == CompareOp::EQ)
        return ScanType::INDEX_SCAN;
    return ScanType::TABLE_SCAN;
}

QueryPlan Optimizer::buildSelectPlan(const ParsedStatement& stmt) {
    QueryPlan plan;
    plan.table_name = stmt.table_name;
    plan.where = stmt.where;
    plan.has_where = stmt.has_where;
    plan.join = stmt.join;
    plan.has_join = stmt.has_join;

    const TableDef* table = catalog_.getTable(stmt.table_name);
    if (table) {
        plan.scan = chooseScan(*table, stmt.where, stmt.has_where);
        if (stmt.has_where) plan.selectivity = estimateSelectivity(*table, stmt.where);
    }

    size_t rows = 100;
    if (table) {
        HeapFile* hf = catalog_.getHeapFile(table->name);
        if (hf) rows = max<size_t>(1, hf->estimateRowCount());
    }
    plan.estimated_cost = (double)rows * plan.selectivity;
    if (plan.scan == ScanType::INDEX_SCAN) plan.estimated_cost *= 0.1;

    if (stmt.has_join) {
        size_t left_rows = 100, right_rows = 100;
        HeapFile* l = catalog_.getHeapFile(stmt.join.left_table);
        HeapFile* r = catalog_.getHeapFile(stmt.join.right_table);
        if (l) left_rows = max<size_t>(1, l->estimateRowCount());
        if (r) right_rows = max<size_t>(1, r->estimateRowCount());
        if (left_rows <= right_rows) {
            plan.join_outer_table = stmt.join.left_table;
            plan.join_inner_table = stmt.join.right_table;
        } else {
            plan.join_outer_table = stmt.join.right_table;
            plan.join_inner_table = stmt.join.left_table;
        }
        plan.estimated_cost = (double)(left_rows * right_rows) * 0.01;
    }
    return plan;
}

}  // namespace minidb
