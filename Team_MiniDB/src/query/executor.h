#pragma once
#include <string>
#include <vector>

#include "../catalog/catalog.h"
#include "../recovery/wal_manager.h"
#include "../txn/transaction.h"
#include "optimizer.h"

namespace minidb {

struct QueryResult {
  bool ok = true;
  std::string message;
  std::vector<std::string> column_names;
  std::vector<std::vector<std::string>> rows;  // stringified for display
  std::string explain;
};

// Ties parsing -> optimizer -> plan execution -> catalog/storage mutation
// together. WAL logging is opt-in: pass a non-null `wal` and a non-zero
// `txn_id` to have INSERT/DELETE write before/after-image log records
// (used when running inside an explicit BEGIN...COMMIT transaction);
// callers that don't care about durability/recovery (tests, ad-hoc
// queries) can omit both and get the old un-logged behavior.
class Executor {
 public:
  explicit Executor(Catalog *catalog) : catalog_(catalog), optimizer_(catalog) {}

  QueryResult Execute(const Statement &stmt, txn_id_t txn_id = 0, WalManager *wal = nullptr);

 private:
  QueryResult ExecCreateTable(const CreateTableStmt &s);
  QueryResult ExecInsert(const InsertStmt &s, txn_id_t txn_id, WalManager *wal);
  QueryResult ExecSelect(const SelectStmt &s);
  QueryResult ExecDelete(const DeleteStmt &s, txn_id_t txn_id, WalManager *wal);

  Catalog *catalog_;
  Optimizer optimizer_;
};

}  // namespace minidb
