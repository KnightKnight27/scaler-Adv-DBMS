#pragma once

#include <memory>
#include <string>
#include <vector>

#include "catalog/catalog.h"
#include "executor/executor.h"
#include "executor/execution_metrics.h"
#include "optimizer/optimizer.h"
#include "parser/sql_parser.h"
#include "recovery/recovery_manager.h"
#include "recovery/wal.h"
#include "storage/buffer_pool.h"
#include "storage/page_manager.h"
#include "transaction/lock_manager.h"
#include "transaction/transaction_manager.h"

namespace minidb {

class Database {
public:
    explicit Database(std::string db_path);
    ~Database();

    std::string ExecuteSql(const std::string& sql);
    void Recover();
    void Crash();
    void SetBatchMode(bool enabled) { use_batch_ = enabled; }
    bool batch_mode() const { return use_batch_; }
    BufferPool* buffer_pool() { return buffer_pool_.get(); }
    ExecutionMetrics GetExecutionMetrics() const { return ExecutionMetricsHolder::Get(); }
    void ResetBufferPoolCounters() { buffer_pool_->ResetCounters(); }
    void Flush();

private:
    std::string db_path_;
    std::unique_ptr<PageManager> page_manager_;
    std::unique_ptr<BufferPool> buffer_pool_;
    std::unique_ptr<WriteAheadLog> wal_;
    std::unique_ptr<LockManager> lock_manager_;
    std::unique_ptr<TransactionManager> txn_manager_;
    std::unique_ptr<RecoveryManager> recovery_manager_;
    Catalog catalog_;
    SqlParser parser_;
    Optimizer optimizer_{50.0};
    bool use_batch_ = false;

    std::string HandleCreateTable(const CreateTableStmt& stmt);
    std::string FormatResult(const std::vector<std::vector<std::string>>& rows);
};

}  // namespace minidb
