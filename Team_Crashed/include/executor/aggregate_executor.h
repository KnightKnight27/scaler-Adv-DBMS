// =============================================================================
// include/executor/aggregate_executor.h
// -----------------------------------------------------------------------------
// AggregateExecutor: materialises the child into memory, computes the
// requested aggregates, and emits a single Tuple per group.
//
// Aggregate functions (parser::ExprKind::FUNCTION_CALL):
//   COUNT(*) / COUNT(col)  -> INT     (NULL values are not counted for COUNT(col))
//   SUM(col)               -> INT/FLOAT (NULLs skipped)
//   AVG(col)               -> FLOAT   (NULLs skipped)
//   MIN(col)               -> same type as the input
//   MAX(col)               -> same type as the input
//
// When `groupBy` is empty we emit exactly one Tuple with the global
// aggregate. When `groupBy` is non-empty we hash by the concatenated
// string form of the group-key values (small-cardinality groups are
// fine; for high-cardinality we'd swap in a proper hash table).
// =============================================================================
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "executor/executor.h"
#include "parser/ast.h"

namespace minidb::executor {

class AggregateExecutor : public Executor {
public:
    AggregateExecutor(ExecutorContext* ctx,
                      std::unique_ptr<Executor> child,
                      std::vector<std::unique_ptr<parser::Expr>> projectionExprs,
                      std::vector<parser::Expr> groupBy);
    ~AggregateExecutor() override;

    Status init() override;
    Status next (Tuple& out) override;
    Status close() override;

private:
    // Per-aggregate accumulator state.
    struct AggState {
        std::string   func;     // COUNT, SUM, AVG, MIN, MAX
        std::string   col;      // column name (empty for COUNT(*))
        bool          isStar = false;
        bool          anyValue = false;
        double        sumF = 0.0;
        int64_t       sumI = 0;
        int64_t       count = 0;
        Value         min;
        Value         max;
    };

    // State for one group. When groupBy is empty, we keep exactly one
    // pseudo-group keyed by the empty string.
    struct Group {
        std::vector<AggState> aggs;
        std::vector<Value>    key;   // group-by values for the projection
    };

    std::unique_ptr<Executor>                    child_;
    std::vector<std::unique_ptr<parser::Expr>>   projectionExprs_;
    std::vector<parser::Expr>                    groupBy_;
    std::unordered_map<std::string, std::size_t> groupIndex_;
    std::vector<Group>                           groups_;
    std::size_t                                  cursor_ = 0;
};

} // namespace minidb::executor