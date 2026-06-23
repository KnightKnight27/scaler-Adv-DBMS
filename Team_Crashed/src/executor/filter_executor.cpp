// =============================================================================
// src/executor/filter_executor.cpp
// -----------------------------------------------------------------------------
// FilterExecutor: predicate pass-through over a child.
// =============================================================================
#include "executor/filter_executor.h"

#include <memory>

#include "catalog/schema.h"
#include "executor/executor.h"
#include "executor/predicate_eval.h"
#include "parser/ast.h"

namespace minidb::executor {

FilterExecutor::FilterExecutor(ExecutorContext* ctx,
                               std::unique_ptr<Executor> child,
                               std::unique_ptr<parser::Expr> predicate,
                               catalog::Schema schema)
    : Executor(ctx), child_(std::move(child)),
      predicate_(std::move(predicate)),
      schema_(std::move(schema)) {}

// Out-of-line destructor so the unique_ptr members get a TU-local one.
FilterExecutor::~FilterExecutor() = default;

Status FilterExecutor::init() {
    if (!child_) return Status::INVALID_ARGUMENT;
    return child_->init();
}

// Pull rows from the child until we find one that passes the predicate.
// When `predicate_` is null we are a pass-through.
Status FilterExecutor::next(Tuple& out) {
    if (!child_) return Status::DONE;
    if (!predicate_) {
        return child_->next(out);
    }
    Tuple t;
    while (child_->next(t) == Status::OK) {
        if (evalPredicate(*predicate_, t, schema_)) {
            out = std::move(t);
            return Status::OK;
        }
    }
    return Status::DONE;
}

Status FilterExecutor::close() {
    if (child_) return child_->close();
    return Status::OK;
}

} // namespace minidb::executor