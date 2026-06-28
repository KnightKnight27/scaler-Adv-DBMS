#pragma once

#include "executor/executor.h"
#include "parser/parser.h"
#include <memory>
#include <string>

// Forward declaration
class HeapEngine;

// ============================================================
// TableStats — per-table statistics for cost estimation
// ============================================================
struct TableStats {
    int row_count = 0;
    int num_pages = 1;
    int pk_min = 0;
    int pk_max = 0;
};

// ============================================================
// Cost-Based Optimizer
//
// Decides: SeqScan vs IndexScan based on estimated cost.
// Selectivity: equality = 1/row_count, range = fraction of domain.
// Join order: smaller estimated cardinality goes outer.
// ============================================================

// Analyze WHERE clause to extract index-applicable predicate on PK.
// Returns: whether an index scan is possible, the key/range, and
// any remaining filter predicates not covered by the index.
struct IndexCondition {
    bool can_use_index = false;
    bool is_exact = false;      // pk = constant
    int exact_key = 0;
    int range_low = 0;          // for range: pk >= low
    int range_high = 0;         //            pk <= high
    ExprPtr remaining_filter;   // predicates NOT handled by index
};

IndexCondition AnalyzeWhere(const ExprPtr& where, const Schema& schema);

// Estimate selectivity (fraction of rows passing the predicate)
double EstimateSelectivity(const IndexCondition& cond, const TableStats& stats);

// Create the optimal plan for a SELECT statement
std::unique_ptr<PlanNode> CreateSelectPlan(const SelectStmt& stmt, HeapEngine* engine);
