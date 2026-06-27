// =============================================================================
// src/executor/limit_executor.cpp
// -----------------------------------------------------------------------------
// LimitExecutor: pass-through with a counter.
// =============================================================================
#include "executor/limit_executor.h"

#include <memory>

#include "executor/executor.h"

namespace minidb::executor {

LimitExecutor::LimitExecutor(ExecutorContext* ctx,
                             std::unique_ptr<Executor> child,
                             int limit)
    : Executor(ctx), child_(std::move(child)), limit_(limit) {}

LimitExecutor::~LimitExecutor() = default;

Status LimitExecutor::init() {
    if (!child_) return Status::INVALID_ARGUMENT;
    produced_ = 0;
    return child_->init();
}

// Stop after `limit_` rows have been emitted.
Status LimitExecutor::next(Tuple& out) {
    if (!child_) return Status::DONE;
    if (limit_ >= 0 && produced_ >= limit_) return Status::DONE;
    Status s = child_->next(out);
    if (s == Status::OK) ++produced_;
    return s;
}

Status LimitExecutor::close() {
    if (child_) return child_->close();
    return Status::OK;
}

} // namespace minidb::executor