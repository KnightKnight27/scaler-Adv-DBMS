// MiniDB - cost-based optimizer.
// Two decisions, both driven by catalog statistics (row counts) and a small cost model:
//   1. Access path: sequential scan vs B+Tree index scan for an equality predicate.
//   2. Join order: which table drives the nested loop (the smaller one, as the outer side).
// Selectivity for an equality is 1/num_rows on a unique index, else a default fraction.
#pragma once

#include <string>

#include "../catalog/catalog.h"
#include "../parser/ast.h"

namespace minidb {

struct ScanPlan {
    bool use_index = false;
    IndexInfo* index = nullptr;
    Value key;            // lookup key when use_index
    double seq_cost = 0;
    double index_cost = 0;
    double selectivity = 1.0;
    std::string desc;     // human-readable choice, for EXPLAIN
};

class Optimizer {
public:
    // Decide the access path for scanning `t` under an optional WHERE.
    static ScanPlan ChooseScan(TableInfo* t, const Expr* where);

    // For a two-table join, return true if the tables should be swapped so the
    // smaller table becomes the outer (driving) relation.
    static bool ShouldSwapJoin(uint64_t outer_rows, uint64_t inner_rows) {
        return inner_rows < outer_rows;
    }

    static constexpr double kDefaultEqSelectivity = 0.10;
};

}  // namespace minidb
