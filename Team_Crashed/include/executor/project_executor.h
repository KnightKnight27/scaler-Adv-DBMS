// =============================================================================
// include/executor/project_executor.h
// -----------------------------------------------------------------------------
// ProjectExecutor: applies a column-list projection over a child executor.
//
// `outputColumns` is the simplified "* or names only" list used when the
// parser emitted simple column references. `projectionExprs` carries the
// full Expr list including any function calls (COUNT, SUM, AVG, MIN, MAX).
//
// Output format:
//   - When projectionExprs is non-empty, we evaluate each expression in
//     turn and emit a Tuple whose values match the expressions 1:1. A
//     column reference resolves against the child's tuple; a function
//     call is delegated to the aggregate executor before projection.
//   - When projectionExprs is empty, we fall back to outputColumns and
//     emit only the named columns in the order given. If outputColumns
//     is also empty, the child's row passes through unchanged.
// =============================================================================
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "catalog/schema.h"
#include "executor/executor.h"
#include "parser/ast.h"

namespace minidb::executor {

class ProjectExecutor : public Executor {
public:
    ProjectExecutor(ExecutorContext* ctx,
                    std::unique_ptr<Executor> child,
                    std::vector<std::string> outputColumns,
                    std::vector<std::unique_ptr<parser::Expr>> projectionExprs,
                    catalog::Schema childSchema = {});
    ~ProjectExecutor() override;

    Status init() override;
    Status next (Tuple& out) override;
    Status close() override;

private:
    std::unique_ptr<Executor>                    child_;
    std::vector<std::string>                     outputColumns_;
    std::vector<std::unique_ptr<parser::Expr>>   projectionExprs_;
    catalog::Schema                              childSchema_;
};

} // namespace minidb::executor
