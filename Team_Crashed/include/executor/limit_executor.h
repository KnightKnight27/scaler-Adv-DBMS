// =============================================================================
// include/executor/limit_executor.h
// -----------------------------------------------------------------------------
// LimitExecutor: pass-through up to `limit` rows, then DONE.
// =============================================================================
#pragma once

#include <memory>

#include "executor/executor.h"

namespace minidb::executor {

class LimitExecutor : public Executor {
public:
    LimitExecutor(ExecutorContext* ctx,
                  std::unique_ptr<Executor> child,
                  int limit);
    ~LimitExecutor() override;

    Status init() override;
    Status next (Tuple& out) override;
    Status close() override;

private:
    std::unique_ptr<Executor> child_;
    int                       limit_;
    int                       produced_ = 0;
};

} // namespace minidb::executor