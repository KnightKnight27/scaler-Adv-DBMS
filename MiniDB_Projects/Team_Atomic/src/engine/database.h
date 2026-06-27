#pragma once
#include <memory>
#include <string>
#include <vector>
#include "common/types.h"
#include "catalog/catalog.h"
#include "exec/executor.h"
#include "sql/ast.h"
#include "recovery/wal.h"
#include "txn/transaction_manager.h"

namespace minidb {

// Result of executing one statement.
struct QueryResult {
  bool is_select = false;
  OutSchema schema;
  std::vector<Row> rows;
  int affected = 0;        // rows inserted/deleted
  std::string message;     // human-readable status
  std::string plan;        // optimizer/explain breadcrumb
};

// The top-level embedded database: owns storage, catalog, the SQL engine,
// the lock manager (2PL), and the write-ahead log (crash recovery).
class Database {
 public:
  explicit Database(const std::string& db_path);
  ~Database();

  // Parse and execute a single SQL statement. Throws DBError on failure.
  QueryResult Execute(const std::string& sql);

  Catalog* GetCatalog() { return catalog_.get(); }
  BufferPoolManager* GetBufferPool() { return bpm_.get(); }
  TransactionManager* GetTxnManager() { return txn_mgr_.get(); }

  // Persist everything and clear the WAL (a clean checkpoint).
  void Checkpoint();

  // TEST ONLY: simulate a process crash (no checkpoint; dirty pages lost, WAL kept).
  void SimulateCrash();

 private:
  QueryResult ExecCreate(const Statement& s);
  QueryResult ExecInsert(const Statement& s);
  QueryResult ExecSelect(const Statement& s);
  QueryResult ExecDelete(const Statement& s);
  QueryResult ExecBegin();
  QueryResult ExecCommit();
  QueryResult ExecAbort();

  // Apply a logical row op to heap+index (idempotent by primary key).
  void ApplyInsert(TableMeta* meta, int64_t key, const std::string& row);
  void ApplyDelete(TableMeta* meta, int64_t key);

  // Replay the WAL after restart: redo committed txns, undo the rest.
  void Recover();

  // The active transaction id (autocommit allocates a fresh one per statement).
  txn_id_t CurrentTxn();
  void EndAutocommit(txn_id_t txn);  // log COMMIT + flush + release if implicit

  std::string db_path_;
  std::unique_ptr<DiskManager> disk_;
  std::unique_ptr<BufferPoolManager> bpm_;
  std::unique_ptr<Catalog> catalog_;
  std::unique_ptr<WAL> wal_;
  std::unique_ptr<TransactionManager> txn_mgr_;

  // Explicit-transaction state (single connection / shell).
  bool in_txn_ = false;
  txn_id_t cur_txn_ = INVALID_TXN_ID;
  // Applied ops of the active explicit txn, kept so ABORT can undo them.
  struct AppliedOp { LogType type; std::string table; int64_t key; std::string row; };
  std::vector<AppliedOp> txn_ops_;
  bool crashed_ = false;
};

}  // namespace minidb
