#pragma once

#include "storage/disk.h"
#include "storage/buffer.h"
#include "index/btree.h"
#include "query/parser.h"
#include "query/operators.h"
#include "query/optimizer.h"
#include "concurrency/lock_manager.h"
#include "concurrency/mvcc.h"
#include "recovery/log_manager.h"
#include "recovery/recovery_manager.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

enum class ConcurrencyMode {
    TWO_PHASE_LOCKING, // 2PL
    MULTI_VERSION_CONCURRENCY_CONTROL // MVCC
};

struct TableMetadata {
    std::string name;
    std::vector<std::string> schema; // Column names e.g., ["users.id", "users.name", "users.age"]
    PageId_t first_page_id = INVALID_PAGE_ID;
    PageId_t last_page_id = INVALID_PAGE_ID;
    PageId_t index_root_page_id = INVALID_PAGE_ID;
    std::unique_ptr<BPlusTree> index;
};

class Database {
public:
    Database(const std::string& db_file, const std::string& log_file, ConcurrencyMode mode = ConcurrencyMode::TWO_PHASE_LOCKING);
    ~Database();

    // Transaction Management
    Transaction* BeginTransaction();
    bool CommitTransaction(Transaction* txn);
    void AbortTransaction(Transaction* txn);

    // SQL execution
    bool ExecuteSQL(Transaction* txn, const std::string& sql, std::vector<Tuple>& output_rows, std::vector<std::string>& output_schema);

    // Database Control
    void SimulateCrash();
    void RestartAndRecover();
    void SetConcurrencyMode(ConcurrencyMode mode);
    ConcurrencyMode GetConcurrencyMode() const { return mode_; }

    // Catalog Helpers
    void CreateTable(const std::string& table_name, const std::vector<std::string>& schema);
    TableMetadata* GetTableMetadata(const std::string& table_name);
    
    // Stats & Debug
    BufferPoolManager* GetBufferPoolManager() { return bpm_.get(); }
    LockManager* GetLockManager() { return lock_manager_.get(); }
    MVCCManager* GetMVCCManager() { return mvcc_manager_.get(); }
    LogManager* GetLogManager() { return log_manager_.get(); }
    void PrintBPlusTree(const std::string& table_name);
    void PrintBufferPoolStats();
    void ClearDatabase();

private:
    std::string db_file_name_;
    std::string log_file_name_;
    ConcurrencyMode mode_;

    std::unique_ptr<DiskManager> disk_manager_;
    std::unique_ptr<BufferPoolManager> bpm_;
    std::unique_ptr<LockManager> lock_manager_;
    std::unique_ptr<MVCCManager> mvcc_manager_;
    std::unique_ptr<LogManager> log_manager_;
    std::unique_ptr<RecoveryManager> recovery_manager_;

    std::unordered_map<std::string, TableMetadata> catalog_;
    std::unordered_map<TxId_t, Transaction*> active_transactions_;
    TxId_t next_txn_id_ = 1;
    std::mutex catalog_latch_;

    Optimizer optimizer_;

    // Chaining insertion into heap pages
    RID InsertTupleInternal(Transaction* txn, const std::string& table_name, const std::vector<std::string>& values);
    bool DeleteTupleInternal(Transaction* txn, const std::string& table_name, const RID& rid, int32_t key);
    void RebuildIndexes();
};
