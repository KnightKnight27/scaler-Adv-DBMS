#pragma once

#include <atomic>
#include <memory>

#include "catalog/catalog.h"
#include "common/status.h"
#include "recovery/log_manager.h"
#include "txn/concurrency_lock_manager.h"
#include "txn/transaction.h"

namespace axiomdb {

// ---------------------------------------------------------------------------
// TxnManager -- the lifecycle coordinator tying the lock manager, the
// WAL, and per-transaction undo together.
//
//   begin()  -> assigns an id, logs a Begin record
//   commit() -> logs Commit and fsyncs the WAL (the durability point), then
//               releases all locks (strict 2PL)
//   abort()  -> replays the in-memory undo log in reverse (delete inserted rows,
//               re-insert deleted rows), logs Abort, releases all locks
// ---------------------------------------------------------------------------
class TxnManager {
 public:
  TxnManager(ConcurrencyLockManager* lock_mgr, LogManager* wal, Catalog* catalog)
      : lock_mgr_(lock_mgr), wal_(wal), catalog_(catalog) {}

  std::unique_ptr<Transaction> begin();
  Status commit(Transaction* txn);
  void abort(Transaction* txn);

  ConcurrencyLockManager* lock_manager() { return lock_mgr_; }
  LogManager* wal() { return wal_; }

 private:
  ConcurrencyLockManager* lock_mgr_;
  LogManager* wal_;
  Catalog* catalog_;
  std::atomic<txn_id_t> next_id_{1};
};

}  // namespace axiomdb
