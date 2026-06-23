// Cost-based optimizer: turns a parsed SELECT into a physical operator tree.
//
// What it decides:
//   * Access path per table -- SeqScan vs. primary-key IndexScan, based on
//     whether WHERE has a usable predicate on the primary key and how selective
//     it is (selectivity estimation).
//   * Join order -- a greedy ordering that scans the most selective (smallest
//     estimated cardinality) table first, building a left-deep nested-loop tree.
//   * Predicate placement -- single-table predicates are pushed down to filters
//     right above each scan; multi-table predicates become join conditions.
//
// It also emits a human-readable EXPLAIN string for the demo / viva.
#pragma once

#include <string>
#include <vector>

#include "execution/executor.hpp"
#include "sql/ast.hpp"

namespace minidb {

struct BaseTable {
    std::string alias;
    TableAccess access;
};

struct PhysicalPlan {
    OpPtr root;
    std::string explain;
};

class Optimizer {
public:
    PhysicalPlan optimize(SelectStmt* stmt, std::vector<BaseTable> tables);

private:
    // Estimated number of rows a table contributes after local filters.
    double estimate_cardinality(const BaseTable& bt,
                                const std::vector<ExprPtr>& local_preds);
    // Pick the access-path operator for one table (index vs seq scan).
    OpPtr make_scan(const BaseTable& bt, const std::vector<ExprPtr>& local_preds,
                    std::string& how);
};

}  // namespace minidb
