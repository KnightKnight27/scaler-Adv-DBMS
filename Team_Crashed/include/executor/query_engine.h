// =============================================================================
// include/executor/query_engine.h
// -----------------------------------------------------------------------------
// QueryEngine: the façade the CLI and benchmarks call. Hands off SQL to
// the parser, optimizer, and executor pipeline.
// =============================================================================
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "executor/executor.h"
#include "recovery/recovery_manager.h"
#include "recovery/wal.h"

namespace minidb::planner  { class Optimizer; }

namespace minidb::executor {

class QueryEngine {
public:
    QueryEngine(storage::BufferPool*               bp,
                catalog::CatalogManager*           cat,
                index::IndexManager*               idx,
                transaction::TransactionManager*   txn,
                recovery::RecoveryManager*         rec,
                recovery::WAL*                     wal = nullptr);

    ~QueryEngine();

    QueryEngine(const QueryEngine&)            = delete;
    QueryEngine& operator=(const QueryEngine&) = delete;

    // For SELECT. Returns all matching tuples.
    std::vector<Tuple> execute(const std::string& sql);

    // For INSERT / DELETE / CREATE / DROP / BEGIN / COMMIT / ROLLBACK.
    Status executeUpdate(const std::string& sql);

    // Column names of the most recent SELECT's result set, in output order
    // (mirrors what ProjectExecutor emits). Empty for non-SELECT calls or
    // when no plan was produced. The CLI prints this as a header row so
    // users can see the column names above the data.
    const std::vector<std::string>& lastOutputColumns() const noexcept {
        return lastOutputColumns_;
    }

private:
    ExecutorContext           ctx_;
    recovery::WAL*            wal_;
    std::unique_ptr<planner::Optimizer> optimizer_;
    // executors are constructed per call, not stored.
    std::vector<std::string>  lastOutputColumns_;
};

} // namespace minidb::executor