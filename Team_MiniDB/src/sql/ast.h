#pragma once
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../catalog/schema.h"
#include "../catalog/value.h"

namespace minidb {

enum class CompareOp { EQ, NEQ, LT, LTE, GT, GTE };

// Reference to a column, optionally qualified by table/alias (needed once
// a query joins two tables and column names could collide).
struct ColumnRef {
  std::string table;  // empty if unqualified
  std::string column;
};

// One operand of a comparison: either a column reference or a literal.
struct Operand {
  bool is_column;
  ColumnRef column;
  Value literal;
};

struct Comparison {
  Operand left;
  CompareOp op;
  Operand right;
};

struct CreateTableStmt {
  std::string table_name;
  std::vector<Column> columns;
};

struct InsertStmt {
  std::string table_name;
  std::vector<Value> values;
};

struct DeleteStmt {
  std::string table_name;
  std::vector<Comparison> where;
};

struct SelectStmt {
  std::vector<ColumnRef> select_cols;  // empty means SELECT *
  std::string from_table;
  std::string from_alias;
  // optional single inner join
  bool has_join = false;
  std::string join_table;
  std::string join_alias;
  Comparison join_cond;
  std::vector<Comparison> where;
};

enum class StmtKind { CREATE_TABLE, INSERT, SELECT, DELETE };

struct Statement {
  StmtKind kind;
  CreateTableStmt create_table;
  InsertStmt insert;
  SelectStmt select;
  DeleteStmt del;
};

}  // namespace minidb
