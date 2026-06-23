#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// optimizer.h  –  rule-based cost estimator / plan selector
// ─────────────────────────────────────────────────────────────────────────────
#include "query/parser.h"
#include "storage/heap_file.h"
#include <string>
#include <vector>

namespace minidb {

/**
 * Optimizer – chooses between INDEX_SCAN and TABLE_SCAN.
 *
 * Cost model:
 *   Point SELECT on a known key → INDEX_SCAN  (O(log n))
 *   Full SELECT / SHOW          → TABLE_SCAN  (must visit all rows)
 *   INSERT / DELETE             → TABLE_SCAN  (write goes to heap first)
 */
class Optimizer {
public:
    std::string SelectPlan(const Statement& stmt, size_t total_records, const std::vector<Record>& sample_data) const;
};

} // namespace minidb
