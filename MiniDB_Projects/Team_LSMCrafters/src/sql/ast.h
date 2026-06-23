#pragma once
#include <memory>
#include <string>
#include <variant>
#include <vector>
#include "common/types.h"

namespace minidb {

// ---- Expression tree (used in WHERE and JOIN ON) ----
struct Expr {
  virtual ~Expr() = default;
};
using ExprPtr = std::unique_ptr<Expr>;

struct ColumnRef : Expr {
  std::string table;   // optional qualifier ("" if unqualified)
  std::string column;
};

struct IntLit : Expr {
  int64_t value;
};

struct StrLit : Expr {
  std::string value;
};

// op is one of: = != < > <= >= (comparison), AND, OR (boolean)
struct BinaryExpr : Expr {
  std::string op;
  ExprPtr     left;
  ExprPtr     right;
};

// ---- Statements ----
struct SelectStmt {
  std::vector<ColumnRef> select_list;  // empty means "*"
  std::string            from_table;
  bool                   has_join = false;
  std::string            join_table;
  ExprPtr                join_on;   // equality on two columns
  ExprPtr                where;     // may be null
};

struct InsertStmt {
  std::string        table;
  std::vector<Value> values;  // positional, one per column
};

struct DeleteStmt {
  std::string table;
  ExprPtr     where;  // may be null (delete all)
};

using Statement = std::variant<SelectStmt, InsertStmt, DeleteStmt>;

}  // namespace minidb
