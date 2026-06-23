#include "query/optimizer.h"
#include "common/config.h"
#include <cmath>
#include <sstream>
#include <iomanip>

TableStats Optimizer::collectStats(HeapFile& heap) {
    TableStats stats;
    stats.num_records = heap.recordCount();
    
    constexpr size_t avg_records_per_page = 10;
    stats.num_pages = (stats.num_records == 0) ? 1
                      : (stats.num_records + avg_records_per_page - 1) / avg_records_per_page;
    return stats;
}

double Optimizer::costTableScan(const TableStats& stats) const {
    return static_cast<double>(stats.num_pages) * IO_PAGE_COST;
}

double Optimizer::costIndexScan(const TableStats& stats, double selectivity) const {
    if (stats.num_records == 0) {
        return 0.0;
    }
    const double tree_cost = std::log2(static_cast<double>(stats.num_records)) * IO_PAGE_COST;
    const double fetch_cost = selectivity * static_cast<double>(stats.num_records) * IO_PAGE_COST;
    return tree_cost + fetch_cost;
}

QueryPlan Optimizer::choosePlanForKey(const TableStats& stats, bool has_index) const {
    const double scan_cost = costTableScan(stats);

    if (!has_index || stats.num_records == 0) {
        return QueryPlan{ScanStrategy::TABLE_SCAN, scan_cost,
                         "TABLE_SCAN (no index available), cost=" + std::to_string(scan_cost)};
    }

    const double selectivity = (stats.num_distinct_keys() > 0)
                               ? 1.0 / static_cast<double>(stats.num_distinct_keys())
                               : 1.0;
    const double index_cost = costIndexScan(stats, selectivity);

    std::ostringstream desc;
    if (index_cost <= scan_cost) {
        desc << "INDEX_SCAN (selectivity=" << std::fixed << std::setprecision(4) << selectivity
             << ", index_cost=" << index_cost << ", scan_cost=" << scan_cost << ")";
        return QueryPlan{ScanStrategy::INDEX_SCAN, index_cost, desc.str()};
    } else {
        desc << "TABLE_SCAN (scan_cost=" << scan_cost << " < index_cost=" << index_cost << ")";
        return QueryPlan{ScanStrategy::TABLE_SCAN, scan_cost, desc.str()};
    }
}

QueryPlan Optimizer::choosePlanForRange(const TableStats& stats, bool has_index) const {
    const double scan_cost = costTableScan(stats);

    if (!has_index || stats.num_records == 0) {
        return QueryPlan{ScanStrategy::TABLE_SCAN, scan_cost,
                         "TABLE_SCAN (no index), cost=" + std::to_string(scan_cost)};
    }

    constexpr double range_selectivity = 0.5;
    const double index_cost = costIndexScan(stats, range_selectivity);

    if (index_cost <= scan_cost) {
        return QueryPlan{ScanStrategy::INDEX_SCAN, index_cost,
                         "INDEX_SCAN (range query, est. 50% match), cost=" + std::to_string(index_cost)};
    } else {
        return QueryPlan{ScanStrategy::TABLE_SCAN, scan_cost,
                         "TABLE_SCAN (cheaper than index for range), cost=" + std::to_string(scan_cost)};
    }
}

QueryPlan Optimizer::choosePlanForScan(const TableStats& stats) const {
    const double cost = costTableScan(stats);
    return QueryPlan{ScanStrategy::TABLE_SCAN, cost,
                     "TABLE_SCAN (full scan, no WHERE clause), cost=" + std::to_string(cost)};
}
