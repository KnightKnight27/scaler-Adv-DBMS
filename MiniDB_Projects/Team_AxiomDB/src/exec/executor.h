#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "catalog/catalog.h"
#include "exec/expression.h"
#include "parser/ast.h"

namespace axiomdb {

class Transaction;
class ConcurrencyLockManager;
class LogManager;

// Per-statement transactional context: the active transaction, the lock manager
// used for 2PL, and the WAL to log modifications.  Null fields mean "run without
// transactional bookkeeping" (e.g. internal/read-only use).
struct ExecContext {
  Transaction* txn = nullptr;
  ConcurrencyLockManager* lock_mgr = nullptr;
  LogManager* wal = nullptr;
};

// The result of running one statement.
//   * SELECT / EXPLAIN -> is_select, `columns` + `rows` (EXPLAIN puts its plan
//     text in `message`).
//   * INSERT / DELETE  -> `affected` row count.
//   * CREATE / txn     -> human-readable `message`.
//   * any error        -> ok = false, `error` set.
struct ExecResult {
  bool ok = true;
  std::string error;
  bool aborted = false;  // set when the statement failed as a deadlock victim

  bool is_select = false;
  std::vector<std::string> columns;
  std::vector<Row> rows;

  uint64_t affected = 0;
  std::string message;

  static ExecResult fail(std::string e) {
    ExecResult r;
    r.ok = false;
    r.error = std::move(e);
    return r;
  }
};

// ---------------------------------------------------------------------------
// Executor -- runs a parsed statement against the catalog.  SELECT/EXPLAIN go
// through the Planner and the Volcano operator tree; INSERT/DELETE drive the
// Table API directly.  Each call is auto-commit at this layer; the transaction
// manager (locking + WAL) wraps this in a later module.
// ---------------------------------------------------------------------------
class Executor {
 public:
  explicit Executor(Catalog& catalog) : catalog_(catalog) {}

  ExecResult execute(const Statement* stmt, ExecContext* ctx = nullptr);

 private:
  ExecResult exec_select(const SelectStmt& s, ExecContext* ctx);
  ExecResult exec_insert(const InsertStmt& s, ExecContext* ctx);
  ExecResult exec_delete(const DeleteStmt& s, ExecContext* ctx);
  ExecResult exec_create(const CreateTableStmt& s);
  ExecResult exec_explain(const ExplainStmt& s);

  Catalog& catalog_;
};

}  // namespace axiomdb
