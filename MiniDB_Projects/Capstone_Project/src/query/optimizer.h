#pragma once

#include "storage/heap_file.h"
#include <string>

/**
 * @struct TableStats
 * @brief Accumulates data density and page allocation counts from a table.
 */
struct TableStats {
    size_t num_records = 0;   ///< Total record count in the table
    size_t num_pages = 0;     ///< Total physical pages allocated to the table

    /**
     * @brief Estimates the number of distinct keys in the table.
     * Assumes a primary key constraint where distinct keys equal total records.
     */
    size_t num_distinct_keys() const { return num_records; }
};

/**
 * @enum ScanStrategy
 * @brief The plan strategy recommended by the cost-based optimizer.
 */
enum class ScanStrategy {
    TABLE_SCAN,   ///< Full sequential scan of all heap pages
    INDEX_SCAN    ///< Point lookup or range traversal utilizing index pages
};

/**
 * @struct QueryPlan
 * @brief Chosen plan payload containing the chosen scan strategy and its estimated cost.
 */
struct QueryPlan {
    ScanStrategy strategy;
    double estimated_cost;
    std::string description;  ///< Descriptive diagnostic explanation of the choice
};

/**
 * @class Optimizer
 * @brief Chooses the cheapest execution plan based on table stats.
 */
class Optimizer {
public:
    /**
     * @brief Chooses strategy for point lookup queries (e.g. WHERE id = key).
     */
    QueryPlan choosePlanForKey(const TableStats& stats, bool has_index) const;

    /**
     * @brief Chooses strategy for range lookup queries (e.g. WHERE id > key).
     */
    QueryPlan choosePlanForRange(const TableStats& stats, bool has_index) const;

    /**
     * @brief Chooses strategy for full sequential table scans.
     */
    QueryPlan choosePlanForScan(const TableStats& stats) const;

    /**
     * @brief Computes table density statistics from the current HeapFile layout.
     */
    static TableStats collectStats(HeapFile& heap);

private:
    double costTableScan(const TableStats& stats) const;
    double costIndexScan(const TableStats& stats, double selectivity) const;
};
