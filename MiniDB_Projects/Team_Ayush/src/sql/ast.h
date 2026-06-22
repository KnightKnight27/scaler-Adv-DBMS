#pragma once
#include <string>
#include <vector>
#include "common/types.h"
#include "record/schema.h"

namespace minidb {

enum class StmtType { CREATE_TABLE, INSERT, SELECT, DELETE_, UNKNOWN };

enum class CompOp { EQ, NE, LT, LE, GT, GE };

// A single WHERE conjunct: <column> <op> <constant>. The column may be
// qualified with a table name (table.column) for joins.
struct Predicate {
  std::string table;   // qualifier ("" if unqualified)
  std::string column;
  CompOp      op;
  Value       value;
};

struct CreateTableStmt {
  std::string         table;
  std::vector<Column> columns;
  std::string         pk_column;  // "" if no primary key
};

struct InsertStmt {
  std::string        table;
  std::vector<Value> values;
};

// Optional inner join: FROM a JOIN b ON a.x = b.y
struct JoinClause {
  bool        present = false;
  std::string table;
  std::string left_table,  left_col;
  std::string right_table, right_col;
};

struct SelectStmt {
  bool                     explain = false;  // EXPLAIN prefix
  bool                     star = false;     // SELECT *
  std::vector<std::string> select_list;      // qualified or bare column names
  std::string              from_table;
  JoinClause               join;
  std::vector<Predicate>   where;            // ANDed predicates
};

struct DeleteStmt {
  std::string            table;
  std::vector<Predicate> where;
};

// A parsed statement (a tagged union kept as plain members for C++14 clarity).
struct Statement {
  StmtType        type = StmtType::UNKNOWN;
  CreateTableStmt create;
  InsertStmt      insert;
  SelectStmt      select;
  DeleteStmt      del;
};

}  // namespace minidb
