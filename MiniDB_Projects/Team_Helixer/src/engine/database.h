#pragma once
#include <atomic>
#include <memory>
#include <string>
#include "storage/disk_manager.h"
#include "storage/buffer_pool.h"
#include "catalog/catalog.h"
#include "txn/lock_manager.h"
#include "txn/transaction.h"
#include "recovery/log_manager.h"
#include "engine/result.h"

namespace minidb {

// ---------------------------------------------------------------------------
// Database is the top-level engine object. It owns every subsystem and wires
// them together:
//   DiskManager + BufferPool  -> storage
//   Catalog                   -> schemas, heaps, primary-key indexes
//   LockManager               -> strict 2PL, serializable isolation
//   LogManager                -> write-ahead log + crash recovery
//
// Durability/recovery model: the WAL is the source of truth. The data file is
// scratch, rebuilt by replaying the WAL on startup (committed transactions are
// redone; uncommitted ones are simply never replayed = undo). This keeps
// recovery simple and obviously correct.
// ---------------------------------------------------------------------------
class Database {
public:
    explicit Database(const std::string &base_path);
    ~Database();

    // ---- Transaction lifecycle ----
    Transaction *begin();
    void         commit(Transaction *txn);
    void         abort(Transaction *txn);

    // ---- DDL / DML used by the executor (logged) and recovery (not logged) ----
    TableInfo *create_table(const std::string &name, const Schema &schema,
                            Transaction *txn, bool logging = true);
    RID  insert_row(TableInfo *t, const Tuple &row, Transaction *txn, bool logging = true);
    void delete_row(TableInfo *t, const RID &rid, const Tuple &old_row,
                    Transaction *txn, bool logging = true);

    // Parse + execute one SQL statement. If `txn` is null the statement runs in
    // its own auto-committed transaction.
    QueryResult execute(const std::string &sql, Transaction *txn = nullptr);

    // Flush dirty pages + sync data file (a simple checkpoint).
    void checkpoint();

    Catalog          &catalog() { return *catalog_; }
    LockManager      &locks()   { return lock_mgr_; }
    BufferPoolManager &bpm()    { return *bpm_; }
    LogManager       &log()     { return *log_; }

private:
    // Replay the WAL into the freshly-truncated data file at startup.
    void recover();

    std::string                        base_;
    std::unique_ptr<DiskManager>       disk_;
    std::unique_ptr<BufferPoolManager> bpm_;
    std::unique_ptr<Catalog>           catalog_;
    LockManager                        lock_mgr_;
    std::unique_ptr<LogManager>        log_;
    std::atomic<txn_id_t>              next_txn_id_{1};
};

} // namespace minidb
