// ============================================================================
// ast.h  --  The Abstract Syntax Tree: the structured form of a SQL statement.
//
// The parser turns a string like "SELECT name FROM users WHERE age > 30" into
// one of the Statement objects below.  The executor then reads these fields
// instead of re-parsing text.  Each statement type is a small struct; we use
// classic inheritance + a `type` tag so the executor can switch on the kind.
//
// Supported grammar (intentionally small but enough for real queries):
//   CREATE TABLE t (col TYPE [PRIMARY KEY], ...)
//   CREATE INDEX name ON t (col)
//   INSERT INTO t VALUES (v, ...)
//   SELECT col,... | *  FROM t  [JOIN t2 ON t.a = t2.b]  [WHERE pred AND ...]
//   DELETE FROM t [WHERE pred AND ...]
//   BEGIN | COMMIT | ROLLBACK
// ============================================================================
#pragma once

#include "common/common.h"
#include "record/value.h"

namespace minidb {

enum class StmtType {
  kCreateTable, kCreateIndex, kInsert, kSelect, kDelete,
  kBegin, kCommit, kAbort
};

// Comparison operators allowed in a WHERE clause.
enum class CompOp { kEq, kNe, kLt, kLe, kGt, kGe };

// One simple predicate: <column> <op> <constant>, e.g. age > 30.  `table` is the
// qualifier in "users.age" (empty if the column was written unqualified).
struct Predicate {
  string table;
  string column;
  CompOp      op;
  Value       value;
};

// Base class: every statement carries its kind.
struct Statement {
  explicit Statement(StmtType t) : type(t) {}
  virtual ~Statement() = default;
  StmtType type;
};

struct ColumnDef {
  string name;
  TypeId      type;
  bool        primary_key{false};
};

struct CreateTableStmt : Statement {
  CreateTableStmt() : Statement(StmtType::kCreateTable) {}
  string            table;
  vector<ColumnDef> columns;
};

struct CreateIndexStmt : Statement {
  CreateIndexStmt() : Statement(StmtType::kCreateIndex) {}
  string index_name;
  string table;
  string column;
};

struct InsertStmt : Statement {
  InsertStmt() : Statement(StmtType::kInsert) {}
  string        table;
  vector<Value> values;   // one row, in column order
};

// A selected output column.  "*" sets star=true; otherwise (table, column).
struct SelectStmt : Statement {
  SelectStmt() : Statement(StmtType::kSelect) {}
  bool                                       star{false};
  vector<pair<string, string>> columns;  // (table, col)
  string table;             // FROM <table>

  bool        has_join{false};   // optional single INNER JOIN
  string join_table;
  string join_left_table,  join_left_col;
  string join_right_table, join_right_col;

  vector<Predicate> where;  // predicates combined with AND
};

struct DeleteStmt : Statement {
  DeleteStmt() : Statement(StmtType::kDelete) {}
  string            table;
  vector<Predicate> where;
};

// BEGIN / COMMIT / ROLLBACK carry no extra data.
struct TxnStmt : Statement {
  explicit TxnStmt(StmtType t) : Statement(t) {}
};

}  // namespace minidb
