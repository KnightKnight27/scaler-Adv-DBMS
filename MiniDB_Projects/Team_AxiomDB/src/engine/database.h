#pragma once

#include <memory>
#include <string>

#include "buffer/buffer_pool_manager.h"
#include "catalog/catalog.h"
#include "exec/executor.h"
#include "recovery/log_manager.h"
#include "storage/disk_storage_manager.h"
#include "txn/concurrency_lock_manager.h"
#include "txn/transaction.h"
#include "txn/txn_manager.h"

namespace axiomdb {

// ---------------------------------------------------------------------------
// Database -- the top-level facade owning the whole stack for one database and
// running SQL end to end: parse -> plan -> execute, under transactions.
//
//   base path "foo"  ->  "foo.wdb" (heap+index pages)
//                        "foo.catalog" (schema metadata)
//                        "foo.wal" (write-ahead log)
//
// Statements run auto-commit unless wrapped in an explicit BEGIN ... COMMIT /
// ROLLBACK.  On open, the WAL is replayed (crash recovery) and then a checkpoint
// (flush + WAL truncate) consolidates the recovered state.  A clean destructor
// checkpoints; a simulated crash (simulate_crash) skips it, leaving recovery to
// the next open.
// ---------------------------------------------------------------------------
class Database {
 public:
  explicit Database(const std::string& base_path,
                    size_t buffer_frames = BUFFER_POOL_DEFAULT_FRAMES);
  ~Database();

  ExecResult run(const std::string& sql);

  void checkpoint();           // flush dirty pages + fsync + truncate the WAL
  void simulate_crash();       // test hook: drop without a clean checkpoint

  Catalog& catalog() { return catalog_; }
  BufferPoolManager& buffer_pool() { return pool_; }

 private:
  void recover();              // replay the WAL on open

  DiskStorageManager disk_;
  BufferPoolManager pool_;
  LogManager wal_;
  Catalog catalog_;
  ConcurrencyLockManager lock_mgr_;
  TxnManager txn_mgr_;
  Executor exec_;

  std::unique_ptr<Transaction> current_txn_;  // active explicit transaction
  bool crashed_ = false;
};

}  // namespace axiomdb
