// =============================================================================
// include/planner/physical_plan.h
// -----------------------------------------------------------------------------
// Physical-plan node types. The executor maps each PhysicalKind to a
// Volcano-style Init/Next/Close class.
// =============================================================================
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "parser/ast.h"

namespace minidb::planner {

enum class PhysicalKind {
    SEQ_SCAN, INDEX_SCAN,
    FILTER, PROJECT,
    NESTED_LOOP_JOIN, HASH_JOIN,
    AGGREGATE, SORT, LIMIT,
    INSERT, DELETE
};

struct PhysicalPlan {
    PhysicalKind                                kind = PhysicalKind::SEQ_SCAN;
    std::string                                 table;
    std::string                                 indexName;      // INDEX_SCAN only
    std::unique_ptr<parser::Expr>               predicate;
    std::vector<std::unique_ptr<PhysicalPlan>>  children;
    std::vector<std::string>                    outputColumns;  // empty = "*"
    // Full projection expressions including any function calls (COUNT,
    // SUM, AVG, MIN, MAX). The wrapper executors project these onto
    // output columns; the optimizer previously dropped function calls,
    // so we now carry them through verbatim.
    std::vector<std::unique_ptr<parser::Expr>>  projectionExprs;
    std::vector<parser::Expr>                   groupBy;
    std::vector<parser::Expr>                   orderBy;
    bool                                        orderDesc = false;
    int                                         limit = -1;
};

} // namespace minidb::planner