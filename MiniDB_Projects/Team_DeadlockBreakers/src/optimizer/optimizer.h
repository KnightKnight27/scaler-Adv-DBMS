#pragma once

#include "common/types.h"
#include "execution/ast.h"
#include "catalog/catalog.h"
#include <string>

namespace minidb {

/*
 * ScanType: decision output from the optimizer.
 */
enum class ScanType { SEQ_SCAN, INDEX_SCAN };

struct ScanDecision {
    ScanType  scan_type;
    int64_t   index_key_lo;  // for index scan
    int64_t   index_key_hi;
    bool      is_range;
};

/*
 * Optimizer: decides scan strategy and join order.
 *
 * Selectivity estimation:
 *   - Equality on PK: 1 / num_rows
 *   - Range: 1/3 (heuristic)
 *   - Non-indexed column: no index scan possible
 *
 * Decision rule:
 *   - If selectivity < 0.15 AND index exists on predicate column → IndexScan
 *   - Otherwise → SeqScan
 *
 * Join ordering:
 *   - For 2-table joins, put the smaller table on the left (outer loop).
 */
class Optimizer {
public:
    Optimizer() = default;

    // Decide scan strategy for a single-table SELECT
    ScanDecision DecideScan(const SelectStmt* stmt, Catalog* catalog);

    // Decide join order: returns {left_table, right_table}
    std::pair<std::string, std::string>
    DecideJoinOrder(const std::string& table1, const std::string& table2,
                    Catalog* catalog);

    // Estimate selectivity of a predicate on a table
    double EstimateSelectivity(const Predicate& pred, TableInfo* table);
};

} // namespace minidb
