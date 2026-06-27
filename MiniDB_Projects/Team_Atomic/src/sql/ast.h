#pragma once
#include <string>
#include <vector>
#include "common/types.h"

namespace minidb {

// Comparison operators used in WHERE / JOIN ON predicates.
enum class CmpOp { EQ, NE, LT, LE, GT, GE };

inline const char* CmpOpName(CmpOp op) {
  switch (op) {
    case CmpOp::EQ: return "=";
    case CmpOp::NE: return "!=";
    case CmpOp::LT: return "<";
    case CmpOp::LE: return "<=";
    case CmpOp::GT: return ">";
    case CmpOp::GE: return ">=";
  }
  return "?";
}

// A column reference, optionally qualified by table/alias (t.col).
struct ColRef {
  std::string table;   // empty if unqualified
  std::string column;
};

// A single comparison. The left side is always a column; the right side is
// either a literal value or another column (the latter drives joins).
struct Predicate {
  ColRef lhs;
  CmpOp op;
  bool rhs_is_col = false;
  ColRef rhs_col;
  Value rhs_val;
};

enum class StmtType { CreateTable, Insert, Select, Delete, Begin, Commit, Abort };

struct ColumnDef {
  std::string name;
  TypeId type;
  bool primary_key = false;
};

struct JoinClause {
  bool present = false;
  std::string table;
  Predicate on;  // equi-join predicate (t1.a = t2.b)
};

// One parsed SQL statement. A tagged union kept as a plain struct for clarity.
struct Statement {
  StmtType type;

  // CREATE TABLE
  std::string table;
  std::vector<ColumnDef> columns;
  EngineType engine = EngineType::Heap;  // USING HEAP | LSM

  // INSERT
  std::vector<Value> insert_values;

  // SELECT
  bool select_star = false;
  bool count_star = false;            // SELECT COUNT(*)
  std::vector<ColRef> select_columns;
  JoinClause join;

  // SELECT / DELETE
  std::vector<Predicate> where;       // AND-ed predicates
};

}  // namespace minidb
