#pragma once
// Abstract syntax tree for the lean SQL surface:
//   CREATE TABLE, INSERT, SELECT (WHERE), DELETE.
// WHERE is a conjunction (AND) of simple <column> <cmp> <literal> predicates.
#include "minidb/schema.hpp"
#include "minidb/types.hpp"
#include <string>
#include <vector>

namespace minidb {

enum class CmpOp { Eq, Ne, Lt, Le, Gt, Ge };

struct Predicate {
  std::string column;
  CmpOp op;
  Value value;
};

using WhereClause = std::vector<Predicate>;  // ANDed together

struct CreateTableStmt {
  std::string table;
  std::vector<Column> columns;
};

struct InsertStmt {
  std::string table;
  std::vector<Value> values;  // positional, one per column
};

// INNER JOIN <table> ON <left_col> = <right_col>. Equi-join only.
struct JoinClause {
  bool present = false;
  std::string table;       // right-hand table
  std::string left_col;    // column reference (qualified or bare)
  std::string right_col;
};

struct SelectStmt {
  std::vector<std::string> columns;  // empty => SELECT *
  std::string table;                 // left/driving table
  JoinClause join;
  WhereClause where;
  std::vector<std::string> order_by;
};

struct DeleteStmt {
  std::string table;
  WhereClause where;
};

struct CreateIndexStmt {
  std::string name;
  std::string table;
  std::string column;
};

struct AnalyzeStmt {
  std::string table;
};

struct ExplainStmt {
  SelectStmt select;
};

enum class StatementKind {
  CreateTable, Insert, Select, Delete, Begin, Commit, Rollback, CreateIndex,
  Analyze, Explain
};

struct Statement {
  StatementKind kind = StatementKind::CreateTable;
  CreateTableStmt create;
  InsertStmt insert;
  SelectStmt select;
  DeleteStmt remove;
  CreateIndexStmt create_index;
  AnalyzeStmt analyze;
  ExplainStmt explain;
};

}  // namespace minidb
