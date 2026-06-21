#pragma once
#include <string>
#include <vector>
#include "sql/ast.h"
#include "catalog/catalog.h"

namespace minidb {

// How a single table will be read.
enum class AccessMethod { SEQ_SCAN, INDEX_POINT, INDEX_RANGE };

// Plan for accessing one table: the chosen method plus, for index access, the
// key/range to probe. `filters` are the residual predicates the executor must
// still evaluate against each produced row.
struct TablePlan {
    std::string            table;
    AccessMethod           method{AccessMethod::SEQ_SCAN};
    int32_t                point_key{0};        // INDEX_POINT
    int32_t                range_low{0};        // INDEX_RANGE
    int32_t                range_high{0};       // INDEX_RANGE
    std::vector<Predicate> filters;             // residual single-table predicates
    double                 est_rows{0};         // estimated output cardinality
    double                 est_cost{0};         // estimated cost (page/row touches)
};

// A full SELECT plan: an outer table and (optionally) an inner table joined by a
// nested loop. The optimizer fixes the join order (smaller estimated outer).
struct QueryPlan {
    TablePlan   outer;
    bool        has_join{false};
    TablePlan   inner;
    Predicate   join_pred;          // outer.col = inner.col (normalised)
    std::string explanation;        // human-readable EXPLAIN text
};

// The cost-based optimizer. It estimates selectivity of predicates, picks an
// access path per table (sequential vs primary-key index scan), and orders a
// two-table join so the smaller relation drives the nested loop.
class Optimizer {
public:
    explicit Optimizer(Catalog &catalog) : catalog_(catalog) {}

    QueryPlan plan_select(const SelectStmt &stmt);

private:
    // Estimate the fraction of rows passing `p` (0..1).
    double selectivity(const Predicate &p, const TableInfo *ti);
    // Build the access plan for one table given the predicates that apply to it.
    TablePlan plan_table(const std::string &table, const std::vector<Predicate> &preds);

    Catalog &catalog_;
};

} // namespace minidb
