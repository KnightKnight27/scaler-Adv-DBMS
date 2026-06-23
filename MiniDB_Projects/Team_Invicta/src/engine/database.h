#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "catalog/catalog.h"
#include "optimizer/optimizer.h"
#include "recovery/wal.h"
#include "sql/parser.h"
#include "storage/buffer_pool_manager.h"
#include "storage/disk_manager.h"
#include "storage/row_store.h"
#include "txn/lock_manager.h"
#include "txn/transaction_manager.h"

namespace minidb {

// Result of executing one statement. For queries, `columns`/`rows` hold the
// output; for DML/DDL, `message` describes what happened.
struct ExecResult {
  bool                                  ok{true};
  bool                                  is_query{false};
  std::string                           message;
  std::vector<std::string>              columns;
  std::vector<std::vector<std::string>> rows;
  std::string                           explain;  // populated for SELECT
};

// The Database ties every component together: storage, catalog, the SQL parser,
// the optimizer, and the per-table row stores. It is the single entry point —
// `Execute(sql)` parses one statement and dispatches it.
class Database : public PlanContext {
 public:
  explicit Database(const std::string &name);
  ~Database();

  ExecResult Execute(const std::string &sql);

  // Force all dirty state to disk (used before a clean shutdown).
  void Flush();

  // PlanContext: resolve a table name to its (lazily opened) row store.
  RowStore *GetStore(const std::string &table) override;

  Catalog &catalog() { return catalog_; }

 private:
  RowStore *OpenStore(TableInfo *info);

  ExecResult DoCreate(const CreateTableStmt &s);
  ExecResult DoInsert(const InsertStmt &s);
  ExecResult DoSelect(const SelectStmt &s);
  ExecResult DoDelete(const DeleteStmt &s);

  // Statement-scoped transaction: returns the explicit txn if one is open, or
  // begins an implicit autocommit txn.
  struct StmtTxn { Transaction *txn; bool implicit; };
  StmtTxn StmtBegin();
  void    StmtCommit(StmtTxn s);
  void    StmtAbort(StmtTxn s);

  // Replay the WAL on startup: redo committed transactions, undo the rest.
  void Recover();

  std::string                                          name_;
  std::unique_ptr<DiskManager>                         disk_;
  std::unique_ptr<BufferPoolManager>                   bpm_;
  Catalog                                              catalog_;
  Parser                                               parser_;
  Optimizer                                            optimizer_;
  LockManager                                          lock_mgr_;
  TransactionManager                                   txn_mgr_;
  WAL                                                  wal_;
  Transaction                                         *current_txn_{nullptr};  // explicit BEGIN
  std::unordered_map<std::string, std::unique_ptr<RowStore>> stores_;
};

}  // namespace minidb
