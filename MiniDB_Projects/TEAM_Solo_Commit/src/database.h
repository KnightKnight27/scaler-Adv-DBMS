// MiniDB - the top-level engine. Owns the storage stack (disk, buffer pool, catalog), the
// concurrency stack (lock manager + transaction manager, Strict 2PL), and the SQL pipeline
// (parser -> planner/optimizer -> executor). MVCC and WAL recovery layer on in M5.
//
// Two ways to run SQL:
//   Execute(sql)        - session mode: BEGIN/COMMIT/ABORT manage one current transaction;
//                         statements outside a transaction run autocommit (no locking).
//   Execute(sql, txn)   - explicit transaction (used by concurrent threads / benchmarks);
//                         reads take S locks, writes take X locks, deadlocks abort the txn.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "catalog/catalog.h"
#include "common/tuple.h"
#include "execution/exec_context.h"
#include "parser/ast.h"
#include "recovery/log_manager.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"
#include "txn/lock_manager.h"
#include "txn/transaction.h"

namespace minidb {

struct Result {
    bool ok = true;
    std::string error;
    bool is_query = false;
    Schema schema;
    std::vector<Tuple> rows;
    int64_t affected = -1;
    std::string message;
    std::string explain;

    static Result Error(const std::string& e) { Result r; r.ok = false; r.error = e; return r; }
};

class Database {
public:
    explicit Database(const std::string& db_file, size_t pool_frames = 64);

    Result Execute(const std::string& sql);                 // session / autocommit
    Result Execute(const std::string& sql, Transaction* t); // explicit transaction

    // Transaction API for programmatic / concurrent use. Begin/Commit/Abort are also logged
    // to the WAL, and Commit flushes it so a committed transaction survives a crash.
    Transaction* BeginTxn();
    void CommitTxn(Transaction* t);
    void AbortTxn(Transaction* t);  // rolls back the txn's inserts, then releases locks

    Catalog* catalog() { return catalog_.get(); }
    BufferPool* buffer_pool() { return bpool_.get(); }
    LockManager* lock_manager() { return &lock_mgr_; }
    TransactionManager* txn_manager() { return txn_mgr_.get(); }
    void Flush() { bpool_->FlushAll(); log_->Flush(); }

private:
    Result Dispatch(Statement* stmt, Transaction* txn);
    Result RunPlanned(Statement* stmt, Transaction* txn);
    void Rollback(Transaction* txn);

    // Crash recovery: rebuild the database by replaying committed records from the WAL.
    void Recover();
    void ApplyInsert(const std::string& table, const std::vector<Value>& row);
    void ApplyDeleteByValues(const std::string& table, const std::vector<Value>& row);

    std::string db_file_;
    size_t pool_frames_;
    std::unique_ptr<DiskManager> disk_;
    std::unique_ptr<BufferPool> bpool_;
    std::unique_ptr<Catalog> catalog_;
    LockManager lock_mgr_;
    std::unique_ptr<TransactionManager> txn_mgr_;
    std::unique_ptr<LogManager> log_;
    Transaction* session_txn_ = nullptr;  // current txn in session mode
    bool recovering_ = false;             // suppresses logging while replaying the WAL
};

}  // namespace minidb
