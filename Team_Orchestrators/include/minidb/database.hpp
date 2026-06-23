#pragma once
// Top-level facade: owns the catalog and storage engine, parses and executes
// SQL statements, and returns results. This is the embedding API.
#include "minidb/catalog.hpp"
#include "minidb/lock_manager.hpp"
#include "minidb/sql/ast.hpp"
#include "minidb/storage/heap_engine.hpp"
#include "minidb/storage/write_ahead_log.hpp"
#include "minidb/transaction_manager.hpp"
#include "minidb/types.hpp"
#include <memory>
#include <string>
#include <vector>

namespace minidb {

struct QueryResult {
  bool is_select = false;
  std::vector<std::string> columns;  // output column names (SELECT)
  std::vector<Tuple> rows;           // result rows (SELECT)
  std::string message;               // status text (DDL/DML)
};

class Database {
 public:
  // base_path: files <base>.data and <base>.catalog are used/created.
  explicit Database(const std::string& base_path);
  ~Database();

  QueryResult execute(const std::string& sql);
  std::vector<std::string> table_names() const { return catalog_.table_names(); }
  void flush();  // persist catalog + buffer pool, checkpoint WAL

  // Test hooks for crash-recovery testing.
  void debug_flush_pages();  // steal: push dirty pages to disk, no catalog save
  void debug_crash();        // abandon: destructor skips the clean shutdown flush

 private:
  QueryResult run_create(const CreateTableStmt& s);
  QueryResult run_create_index(const CreateIndexStmt& s);
  QueryResult run_insert(const InsertStmt& s);
  QueryResult run_select(const SelectStmt& s);
  QueryResult run_delete(const DeleteStmt& s);
  QueryResult run_analyze(const AnalyzeStmt& s);
  QueryResult run_explain(const ExplainStmt& s);

  std::string base_path_;
  std::string catalog_path_;
  Catalog catalog_;
  std::unique_ptr<HeapEngine> engine_;
  LockManager lock_mgr_;
  std::unique_ptr<WriteAheadLog> wal_;
  std::unique_ptr<TransactionManager> txn_;
  bool skip_shutdown_flush_ = false;
};

}  // namespace minidb
