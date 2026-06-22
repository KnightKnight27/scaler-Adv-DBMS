#pragma once
#include "../catalog/catalog.h"
#include "../query/parser.h"
#include <string>

// The two scan strategies we can choose between.
enum class ScanType { SEQ_SCAN, INDEX_SCAN };

// The result of the optimizer's analysis for a SELECT query.
struct QueryPlan {
    ScanType    scan;
    std::string table;
    std::string reason; // human-readable explanation (good for the demo)

    // For joins: which table should be the outer (driving) table.
    std::string join_outer; // empty if no join
    std::string join_inner;
};

// Cost-Based Optimizer.
// Reads table statistics from the Catalog and decides:
//   1. Should we do a full table scan or use the B+ Tree index?
//   2. For a join, which table is the outer loop?
class Optimizer {
public:
    explicit Optimizer(Catalog& catalog);

    QueryPlan plan(SelectStmt* stmt);

private:
    Catalog& cat;

    // Estimate what fraction of rows will pass a WHERE predicate (0.0 to 1.0).
    // A lower selectivity means fewer rows survive, making an index scan attractive.
    double estimateSelectivity(Expr* where, TableInfo* t);
};
