// =============================================================================
// include/executor/filter_executor.h
// -----------------------------------------------------------------------------
// FilterExecutor: pulls rows from a child executor and passes through
// only the rows for which `predicate` evaluates to true.
//
// Volcano-style:
//   init()  — initialise the child.
//   next()  — drain the child until it yields a row that satisfies the
//             predicate; return Status::DONE when the child is empty.
//   close() — close the child.
//
// The predicate is evaluated using evalPredicate() from
// include/executor/predicate_eval.h. The schema is the column list of
// the *child's* output (e.g. the join of two tables). If the schema is
// empty the predicate is evaluated against an empty schema — column
// references will return NULL, which is fine because the optimizer
// pushes down single-table filters to the scan.
// =============================================================================
#pragma once

#include <memory>
#include <vector>

#include "catalog/schema.h"
#include "executor/executor.h"
#include "parser/ast.h"

namespace minidb::executor {

class FilterExecutor : public Executor {
public:
    FilterExecutor(ExecutorContext* ctx,
                   std::unique_ptr<Executor> child,
                   std::unique_ptr<parser::Expr> predicate,
                   catalog::Schema schema);
    ~FilterExecutor() override;

    Status init() override;
    Status next (Tuple& out) override;
    Status close() override;

private:
    std::unique_ptr<Executor>     child_;
    std::unique_ptr<parser::Expr> predicate_;
    catalog::Schema               schema_;
};

} // namespace minidb::executor