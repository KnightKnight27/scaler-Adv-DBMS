#pragma once

#include <memory>
#include <string>
#include <vector>

#include "catalog/schema.h"  // Column
#include "catalog/value.h"   // Value

namespace axiomdb {

// ===========================================================================
// Abstract Syntax Tree for AxiomDB's SQL subset.
//
// This header is the FROZEN CONTRACT between the parser and everything that
// consumes parsed SQL (binder, optimizer, executor).  The parser only
// constructs these nodes; it does not resolve names against the catalog or
// type-check -- that "binding" happens in a later pass.
//
// Supported grammar (informally):
//   CREATE TABLE t (col TYPE [PRIMARY KEY], ...)
//   INSERT INTO t [(c1, c2, ...)] VALUES (e, ...), (e, ...) ...
//   SELECT  <* | item [, item]*>  FROM t [alias]
//           [ [INNER] JOIN t2 [alias] ON expr ]*
//           [ WHERE expr ]
//   DELETE FROM t [WHERE expr]
//   BEGIN | COMMIT | ROLLBACK/ABORT
//   EXPLAIN <select>
//
// Expressions: literals, (qualified) column refs, comparison / logical /
// arithmetic binary ops, unary NOT and negation, parentheses.
// ===========================================================================

// ----- expressions ---------------------------------------------------------

enum class ExprKind { Literal, ColumnRef, Binary, Unary };

enum class BinOp {
  Eq, Ne, Lt, Le, Gt, Ge,   // comparison
  And, Or,                  // logical
  Add, Sub, Mul, Div,       // arithmetic
};

enum class UnOp { Not, Neg };

struct Expr {
  explicit Expr(ExprKind k) : kind(k) {}
  virtual ~Expr() = default;
  ExprKind kind;
};
using ExprPtr = std::unique_ptr<Expr>;

struct LiteralExpr : Expr {
  explicit LiteralExpr(Value v) : Expr(ExprKind::Literal), value(std::move(v)) {}
  Value value;
};

struct ColumnRefExpr : Expr {
  ColumnRefExpr(std::string tbl, std::string col)
      : Expr(ExprKind::ColumnRef), table(std::move(tbl)), column(std::move(col)) {}
  std::string table;   // optional qualifier ("" if unqualified)
  std::string column;
};

struct BinaryExpr : Expr {
  BinaryExpr(BinOp o, ExprPtr l, ExprPtr r)
      : Expr(ExprKind::Binary), op(o), left(std::move(l)), right(std::move(r)) {}
  BinOp op;
  ExprPtr left;
  ExprPtr right;
};

struct UnaryExpr : Expr {
  UnaryExpr(UnOp o, ExprPtr e) : Expr(ExprKind::Unary), op(o), operand(std::move(e)) {}
  UnOp op;
  ExprPtr operand;
};

// ----- statements ----------------------------------------------------------

enum class StmtKind { CreateTable, Insert, Select, Delete, Begin, Commit, Abort, Explain };

struct Statement {
  explicit Statement(StmtKind k) : kind(k) {}
  virtual ~Statement() = default;
  StmtKind kind;
};
using StmtPtr = std::unique_ptr<Statement>;

struct CreateTableStmt : Statement {
  CreateTableStmt() : Statement(StmtKind::CreateTable) {}
  std::string table;
  std::vector<Column> columns;   // name + type + primary_key flag
  bool if_not_exists = false;
};

struct InsertStmt : Statement {
  InsertStmt() : Statement(StmtKind::Insert) {}
  std::string table;
  std::vector<std::string> columns;          // empty => all columns, in order
  std::vector<std::vector<ExprPtr>> rows;    // each row: a list of value expressions
};

// A table named in a FROM / JOIN clause, with an optional alias.
struct TableRef {
  std::string table;
  std::string alias;   // "" => use table name as its own qualifier
};

struct JoinClause {
  TableRef right;
  ExprPtr on;          // join predicate (INNER JOIN ... ON on)
};

// One element of the SELECT list: either "*", "<tbl>.*", or an expression with
// an optional AS alias.
struct SelectItem {
  bool star = false;
  std::string star_table;   // for "t.*"; "" for bare "*"
  ExprPtr expr;             // valid when !star
  std::string alias;        // output name, "" => derived from expr
};

struct SelectStmt : Statement {
  SelectStmt() : Statement(StmtKind::Select) {}
  std::vector<SelectItem> items;   // never empty (a lone "*" is one starred item)
  TableRef from;
  std::vector<JoinClause> joins;
  ExprPtr where;                   // null => no WHERE
};

struct DeleteStmt : Statement {
  DeleteStmt() : Statement(StmtKind::Delete) {}
  std::string table;
  ExprPtr where;                   // null => delete all rows
};

// BEGIN / COMMIT / ROLLBACK carry no payload; the kind distinguishes them.
struct TxnStmt : Statement {
  explicit TxnStmt(StmtKind k) : Statement(k) {}
};

struct ExplainStmt : Statement {
  ExplainStmt() : Statement(StmtKind::Explain) {}
  StmtPtr inner;                   // the statement being explained (a SELECT)
};

}  // namespace axiomdb
