// ============================================================================
// database.h  --  The top-level engine that wires every layer together.
//
// Database owns the storage stack (disk manager, buffer pool), the WAL, the
// catalog, and the lock manager, and exposes a single entry point: run a SQL
// string inside a transaction.  It is where the pieces meet:
//
//   parse -> (lock) -> optimize -> execute -> (log to WAL) -> (update indexes)
//
// Transaction control:
//   * begin() starts a transaction; commit()/abort() end it.
//   * execute() runs one statement inside a given transaction.
//   * executeAutoCommit() wraps a single statement in its own transaction --
//     this is what the REPL uses when you are not inside an explicit BEGIN.
// ============================================================================
#pragma once

#include <atomic>
#include "common/common.h"
#include "catalog/catalog.h"
#include "execution/executor.h"
#include "optimizer/optimizer.h"
#include "recovery/log_manager.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"
#include "sql/ast.h"
#include "txn/lock_manager.h"
#include "txn/transaction.h"

namespace minidb {

class Database {
 public:
  // The result of a statement: column headers + rows for a SELECT, or an
  // informational message + affected-row count for everything else.
  struct Result {
    vector<string> columns;
    vector<Tuple>       rows;
    string              message;
    int                      affected{0};
    string              plan;   // optimizer's chosen plan (for SELECT)
  };

  // Open (or create) a database at <path>.db / .catalog / .wal and run recovery.
  explicit Database(const string &path);
  ~Database();

  // --- transaction control ---
  Transaction *begin();
  void commit(Transaction *txn);
  void abort(Transaction *txn);

  // Run one already-parsed statement inside `txn`.
  Result execute(Statement *stmt, Transaction *txn);

  // parse + run one SQL string in its own transaction (the common case).
  Result executeAutoCommit(const string &sql);

  Catalog *catalog() { return catalog_.get(); }
  BufferPool *buffer_pool() { return bpm_.get(); }

 private:
  // statement handlers
  Result doCreateTable(CreateTableStmt *s);
  Result doCreateIndex(CreateIndexStmt *s);
  Result doInsert(InsertStmt *s, Transaction *txn);
  Result doDelete(DeleteStmt *s, Transaction *txn);
  Result doSelect(SelectStmt *s, Transaction *txn);

  // helpers
  vector<Predicate> predicatesForTable(TableInfo *t,
                                            const vector<Predicate> &all);
  unique_ptr<Executor> buildScan(TableInfo *t, const ScanChoice &c);

  unique_ptr<DiskManager> disk_;
  unique_ptr<BufferPool>  bpm_;
  unique_ptr<LogManager>  log_;
  unique_ptr<Catalog>     catalog_;
  unique_ptr<LockManager> lock_mgr_;
  atomic<txn_id_t>        next_txn_id_{1};
};

}  // namespace minidb
