#include "optimizer/optimizer.h"
#include <algorithm>

namespace minidb {

double Optimizer::EstimateSelectivity(const Predicate& pred, TableInfo* table) {
    uint32_t num_rows = table->GetTupleCount();
    if (num_rows == 0) return 1.0;

    // Check if predicate is on the primary key column
    bool is_pk = (pred.column == table->schema[table->pk_col].name);

    if (is_pk && pred.op == CmpOp::EQ) {
        // Equality on PK: selectivity = 1/N
        return 1.0 / num_rows;
    }

    if (pred.op == CmpOp::EQ) {
        // Equality on non-PK: assume 10 distinct values heuristic
        double ndv = std::min(static_cast<double>(num_rows), 10.0);
        return 1.0 / ndv;
    }

    // Range predicates: 1/3 heuristic
    return 1.0 / 3.0;
}

ScanDecision Optimizer::DecideScan(const SelectStmt* stmt, Catalog* catalog) {
    ScanDecision decision;
    decision.scan_type = ScanType::SEQ_SCAN;
    decision.is_range  = false;
    decision.index_key_lo = 0;
    decision.index_key_hi = 0;

    TableInfo* table = catalog->GetTable(stmt->table_name);
    if (!table || !table->primary_index) return decision;

    // Check if any predicate is on the primary key
    std::string pk_name = table->schema[table->pk_col].name;

    if (stmt->where_clause.logic == LogicOp::OR) {
        return decision; // Fall back to SEQ SCAN for OR clauses
    }

    for (auto& pred : stmt->where_clause.preds) {
        if (pred.column == pk_name) {
            double selectivity = EstimateSelectivity(pred, table);

            // Use index scan if selectivity < 15%
            if (selectivity < 0.15 && table->primary_index) {
                decision.scan_type = ScanType::INDEX_SCAN;

                if (pred.op == CmpOp::EQ && std::holds_alternative<int64_t>(pred.value)) {
                    decision.index_key_lo = std::get<int64_t>(pred.value);
                    decision.index_key_hi = decision.index_key_lo;
                    decision.is_range = false;
                } else if (std::holds_alternative<int64_t>(pred.value)) {
                    // For range, use a simple range
                    int64_t val = std::get<int64_t>(pred.value);
                    if (pred.op == CmpOp::GT || pred.op == CmpOp::GTE) {
                        decision.index_key_lo = (pred.op == CmpOp::GT) ? val + 1 : val;
                        decision.index_key_hi = INT64_MAX;
                    } else {
                        decision.index_key_lo = INT64_MIN;
                        decision.index_key_hi = (pred.op == CmpOp::LT) ? val - 1 : val;
                    }
                    decision.is_range = true;
                }
                return decision;
            }
        }
    }

    return decision;
}

std::pair<std::string, std::string>
Optimizer::DecideJoinOrder(const std::string& table1, const std::string& table2,
                           Catalog* catalog) {
    TableInfo* t1 = catalog->GetTable(table1);
    TableInfo* t2 = catalog->GetTable(table2);

    uint32_t count1 = t1 ? t1->GetTupleCount() : 0;
    uint32_t count2 = t2 ? t2->GetTupleCount() : 0;

    // Smaller table as outer (left) — fewer iterations in nested loop
    if (count1 <= count2) {
        return {table1, table2};
    }
    return {table2, table1};
}

} // namespace minidb
