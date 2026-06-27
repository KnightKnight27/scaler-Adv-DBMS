// =============================================================================
// include/executor/delete_executor.h
// -----------------------------------------------------------------------------
// DeleteExecutor: scans matching rows (via an IndexScan or SeqScan+Filter),
// deletes from the heap file, removes from every index, logs WAL entries.
// =============================================================================
#pragma once

#include <memory>

#include "executor/executor.h"
#include "parser/ast.h"

namespace minidb::executor {

class DeleteExecutor : public Executor {
public:
    DeleteExecutor(ExecutorContext* ctx, std::unique_ptr<parser::DeleteStmt> stmt);
    ~DeleteExecutor() override;

    Status init() override;
    Status next (Tuple& out) override;
    Status close() override;

private:
    // We keep the original DeleteStmt around because we need to re-apply
    // its WHERE predicate while walking the heap file directly (the scan
    // child also uses it, so we clone the predicate for the child).
    std::unique_ptr<parser::DeleteStmt> stmt_;
    std::unique_ptr<parser::Expr>       where_;     // owned copy for re-eval
    std::unique_ptr<Executor>           child_;     // scan that produces the victims
};

} // namespace minidb::executor