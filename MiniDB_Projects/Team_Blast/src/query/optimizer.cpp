#include "query/optimizer.h"
#include "common/config.h"
#include <cmath>
#include <sstream>
#include <iomanip>

// ─── collectStats ─────────────────────────────────────────────────────────────

TableStats Optimizer::collectStats(HeapFile& heap) {
    TableStats stats;
    stats.num_records = heap.recordCount();
    // Estimate num_pages from record count + average record size
    // Each page body holds approximately (PAGE_BODY_SIZE / (MAX_RECORD_SIZE+sizeof(Slot))) records
    constexpr size_t avg_records_per_page = 10;  // conservative estimate
    stats.num_pages = (stats.num_records == 0) ? 1
                      : (stats.num_records + avg_records_per_page - 1) / avg_records_per_page;
    return stats;
}

// ─── Cost formulas ────────────────────────────────────────────────────────────

double Optimizer::costTableScan(const TableStats& stats) const {
    // Read every page in the heap sequentially
    return static_cast<double>(stats.num_pages) * IO_PAGE_COST;
}

double Optimizer::costIndexScan(const TableStats& stats, double selectivity) const {
    if (stats.num_records == 0) return 0.0;
    // Tree traversal: log2(num_records) page reads to reach the leaf
    double tree_cost   = std::log2(static_cast<double>(stats.num_records)) * IO_PAGE_COST;
    // Then fetch the matching records from the heap
    double fetch_cost  = selectivity * static_cast<double>(stats.num_records) * IO_PAGE_COST;
    return tree_cost + fetch_cost;
}

// ─── choosePlanForKey ─────────────────────────────────────────────────────────
// Point query: WHERE id = <key>
// Selectivity = 1 / num_distinct_keys (exactly one match expected).

QueryPlan Optimizer::choosePlanForKey(const TableStats& stats, bool has_index) const {
    double scan_cost = costTableScan(stats);

    if (!has_index || stats.num_records == 0) {
        return QueryPlan{ScanStrategy::TABLE_SCAN, scan_cost,
                         "TABLE_SCAN (no index available), cost=" + std::to_string(scan_cost)};
    }

    double selectivity = (stats.num_distinct_keys() > 0)
                         ? 1.0 / static_cast<double>(stats.num_distinct_keys())
                         : 1.0;
    double index_cost  = costIndexScan(stats, selectivity);

    if (index_cost <= scan_cost) {
        std::ostringstream desc;
        desc << "INDEX_SCAN (selectivity=" << std::fixed << std::setprecision(4) << selectivity
             << ", index_cost=" << index_cost << ", scan_cost=" << scan_cost << ")";
        return QueryPlan{ScanStrategy::INDEX_SCAN, index_cost, desc.str()};
    } else {
        std::ostringstream desc;
        desc << "TABLE_SCAN (scan_cost=" << scan_cost << " < index_cost=" << index_cost << ")";
        return QueryPlan{ScanStrategy::TABLE_SCAN, scan_cost, desc.str()};
    }
}

// ─── choosePlanForRange ───────────────────────────────────────────────────────
// Range query: WHERE id > <key> or WHERE id < <key>
// Selectivity estimate = 0.5 (roughly half the table).

QueryPlan Optimizer::choosePlanForRange(const TableStats& stats, bool has_index) const {
    double scan_cost = costTableScan(stats);

    if (!has_index || stats.num_records == 0) {
        return QueryPlan{ScanStrategy::TABLE_SCAN, scan_cost,
                         "TABLE_SCAN (no index), cost=" + std::to_string(scan_cost)};
    }

    constexpr double range_selectivity = 0.5;
    double index_cost = costIndexScan(stats, range_selectivity);

    if (index_cost <= scan_cost) {
        return QueryPlan{ScanStrategy::INDEX_SCAN, index_cost,
                         "INDEX_SCAN (range query, est. 50% match), cost=" + std::to_string(index_cost)};
    } else {
        return QueryPlan{ScanStrategy::TABLE_SCAN, scan_cost,
                         "TABLE_SCAN (cheaper than index for range), cost=" + std::to_string(scan_cost)};
    }
}

// ─── choosePlanForScan ────────────────────────────────────────────────────────
// Full scan (SELECT * FROM table, no WHERE): always table scan.

QueryPlan Optimizer::choosePlanForScan(const TableStats& stats) const {
    double cost = costTableScan(stats);
    return QueryPlan{ScanStrategy::TABLE_SCAN, cost,
                     "TABLE_SCAN (full scan, no WHERE clause), cost=" + std::to_string(cost)};
}
