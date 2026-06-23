#pragma once
#include <string>
#include <vector>
#include "catalog/catalog.h"  // StorageType
#include "record/schema.h"    // Column
#include "common/types.h"

namespace minidb {

enum class StmtType {
  INVALID, CREATE_TABLE, INSERT, SELECT, DELETE, BEGIN, COMMIT, ROLLBACK
};

enum class CompareOp { EQ, NE, LT, LE, GT, GE };

inline const char *OpName(CompareOp op) {
  switch (op) {
    case CompareOp::EQ: return "=";
    case CompareOp::NE: return "!=";
    case CompareOp::LT: return "<";
    case CompareOp::LE: return "<=";
    case CompareOp::GT: return ">";
    case CompareOp::GE: return ">=";
  }
  return "?";
}

// A single conjunct of a WHERE clause: <column> <op> <literal>. The column may
// be qualified (table.col); it is resolved against the operator's output schema.
struct Predicate {
  std::string column;
  CompareOp   op{CompareOp::EQ};
  Value       value;
};

struct CreateTableStmt {
  std::string         table;
  std::vector<Column> columns;
  StorageType         storage{StorageType::HEAP};
};

struct InsertStmt {
  std::string        table;
  std::vector<Value> values;  // one row, positional
};

struct JoinClause {
  bool        present{false};
  std::string table;       // right-hand table
  std::string left_col;    // qualified column from the left table
  std::string right_col;   // qualified column from the right table
};

struct SelectStmt {
  bool                     star{false};        // SELECT *
  bool                     count_star{false};  // SELECT COUNT(*)
  std::vector<std::string> columns;            // projected columns (if not star)
  std::string              table;              // FROM table
  JoinClause               join;
  std::vector<Predicate>   where;              // AND-connected
};

struct DeleteStmt {
  std::string            table;
  std::vector<Predicate> where;
};

// A parsed statement. Only the field matching `type` is meaningful.
struct Statement {
  StmtType        type{StmtType::INVALID};
  CreateTableStmt create;
  InsertStmt      insert;
  SelectStmt      select;
  DeleteStmt      del;
};

}  // namespace minidb
