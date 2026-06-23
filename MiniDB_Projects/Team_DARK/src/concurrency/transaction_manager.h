#pragma once

#include "concurrency/lock_manager.h"
#include "recovery/log_manager.h"
#include "storage/buffer_pool_manager.h"
#include "storage/disk_manager.h"
#include "storage/page.h"
#include "storage/table_heap.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace minidb {

struct Transaction {
    TxID id = 0;
    TxID snapshot_xid = 0;
    TxStatus status = TxStatus::ACTIVE;
    bool in_shrinking = false;
    std::unordered_set<TxID> active_txs;
};

class TransactionManager {
public:
    TransactionManager();
    explicit TransactionManager(BufferPoolManager* bpm);
    TransactionManager(const std::string& db_path, const std::string& log_path = "",
                       std::size_t pool_size = 32);

    TransactionManager(const TransactionManager&) = delete;
    TransactionManager& operator=(const TransactionManager&) = delete;

    TxID Begin();
    void Commit(TxID xid);
    void Abort(TxID xid);

    std::optional<std::string> Read(TxID xid, const RowKey& key);
    void Insert(TxID xid, const RowKey& key, const std::string& value);
    RowLocation InsertReturningLocation(TxID xid, const RowKey& key, const std::string& value);
    void Update(TxID xid, const RowKey& key, const std::string& value);
    void Remove(TxID xid, const RowKey& key);

    bool IsCommitted(TxID xid) const;
    bool IsAborted(TxID xid) const;
    TxStatus GetStatus(TxID xid) const;

    LockManager& GetLockManager() { return lock_manager_; }
    BufferPoolManager* GetBufferPoolManager() { return bpm_; }
    TableHeap& GetTableHeap() { return *heap_; }
    const TableHeap& GetTableHeap() const { return *heap_; }
    LogManager* GetLogManager() { return log_manager_.get(); }

    void FlushRecoveryState();

private:
    bool IsVisible(const StoredRowVersion& version, TxID snapshot_xid, TxID reader_xid) const;
    TxID GetSnapshotXid(TxID xid) const;
    Transaction& GetTransaction(TxID xid);
    const Transaction& GetTransaction(TxID xid) const;
    RowLocation MvccInsert(const RowKey& key, const std::string& value, TxID xid);
    void MvccUpdate(const RowKey& key, const std::string& value, TxID xid);
    void MvccDelete(const RowKey& key, TxID xid);
    void RollbackVersions(TxID xid);
    TxID ComputeGlobalXmin() const;
    void GarbageCollect();
    void LogInsertRow(TxID xid, const RowKey& key, const RowLocation& location,
                      const StoredRowVersion& version);
    void LogUpdateXmax(TxID xid, const RowLocation& location, uint64_t old_xmax,
                       uint64_t new_xmax);
    void RunRecoveryIfNeeded();
    static std::string DeriveLogPath(const std::string& db_path, const std::string& log_path);

    mutable std::mutex tx_mutex_;
    std::atomic<TxID> next_xid_;
    std::unordered_map<TxID, Transaction> transactions_;

    std::unique_ptr<DiskManager> owned_disk_;
    std::unique_ptr<BufferPoolManager> owned_bpm_;
    std::unique_ptr<LogManager> log_manager_;
    BufferPoolManager* bpm_;
    std::unique_ptr<TableHeap> heap_;
    std::string db_path_;
    std::string log_path_;
    bool durable_mode_ = false;

    LockManager lock_manager_;
};

}  // namespace minidb
