#pragma once

#include "storage/heap_file.h"
#include <string>

// ─── TableStats ───────────────────────────────────────────────────────────────
// Simple statistics about a table. Used by the optimizer to estimate costs.
// In a real DB these are kept in a catalog and updated by ANALYZE.
// Here we recompute them on the fly from the HeapFile.

struct TableStats {
    size_t num_records = 0;   // estimated number of records
    size_t num_pages   = 0;   // number of heap pages

    // Estimated number of distinct key values.
    // We approximate this as num_records (assuming unique primary keys).
    size_t num_distinct_keys() const { return num_records; }
};

// ─── ScanPlan ─────────────────────────────────────────────────────────────────
// The optimizer's output: which scan strategy to use for a query.

enum class ScanStrategy {
    TABLE_SCAN,   // iterate all pages in the HeapFile
    INDEX_SCAN    // use the B+ tree to find the record directly
};

struct QueryPlan {
    ScanStrategy strategy;
    double       estimated_cost;
    std::string  description;  // human-readable, printed in REPL
};

// ─── Optimizer ────────────────────────────────────────────────────────────────
//
// A simple cost-based plan chooser.
// It computes costs for TABLE_SCAN and INDEX_SCAN and picks the cheaper one.
//
// Cost model:
//   TABLE_SCAN cost  = num_pages * IO_PAGE_COST
//   INDEX_SCAN cost  = log2(num_records) * IO_PAGE_COST   (tree traversal)
//                    + selectivity * num_records * IO_PAGE_COST  (record fetches)
//
// Selectivity for "id = <key>" is 1 / num_distinct_keys (point query).
// Selectivity for "id > <key>" or "id < <key>" is 0.5 (rough estimate).
//
// An index scan is always preferred for point queries on indexed tables.
// A table scan is preferred for full scans (no WHERE clause).

class Optimizer {
public:
    // Choose the best plan for a point query (WHERE id = key) on a table.
    QueryPlan choosePlanForKey(const TableStats& stats, bool has_index) const;

    // Choose the best plan for a range query (WHERE id > key) on a table.
    QueryPlan choosePlanForRange(const TableStats& stats, bool has_index) const;

    // Choose the plan for a full table scan (no WHERE clause).
    QueryPlan choosePlanForScan(const TableStats& stats) const;

    // Build stats from a HeapFile's current state.
    static TableStats collectStats(HeapFile& heap);

private:
    double costTableScan(const TableStats& stats) const;
    double costIndexScan(const TableStats& stats, double selectivity) const;
};
