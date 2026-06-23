// =============================================================================
// include/executor/sort_executor.h
// -----------------------------------------------------------------------------
// SortExecutor: drains the child, sorts the materialised Tuple vector by
// ORDER BY keys, and re-emits one tuple per call.
//
// Implementation is full-materialisation: we keep the entire result in
// memory. That matches the v1 cost model in optimizer.cpp (SORT cost is
// O(n log n) CPU and zero extra pages).
//
// The sort uses the supplied `schema` to map column names in `orderBy`
// to positions in the tuple. For a single-table SELECT the schema is
// the table's schema; for a join it is the concatenation of the two
// schemas in left/right order.
// =============================================================================
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "catalog/schema.h"
#include "executor/executor.h"
#include "parser/ast.h"

namespace minidb::executor {

class SortExecutor : public Executor {
public:
    SortExecutor(ExecutorContext* ctx,
                 std::unique_ptr<Executor> child,
                 std::vector<parser::Expr> orderBy,
                 bool orderDesc,
                 catalog::Schema schema);
    ~SortExecutor() override;

    Status init() override;
    Status next (Tuple& out) override;
    Status close() override;

private:
    std::unique_ptr<Executor>    child_;
    std::vector<parser::Expr>    orderBy_;
    bool                         orderDesc_;
    catalog::Schema              schema_;
    std::vector<Tuple>           buffer_;
    std::size_t                  cursor_ = 0;
};

} // namespace minidb::executor