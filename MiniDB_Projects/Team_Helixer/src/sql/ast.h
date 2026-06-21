#pragma once
#include <string>
#include <vector>
#include <memory>
#include "common/value.h"
#include "catalog/schema.h"

namespace minidb {

// ---------------------------------------------------------------------------
// Abstract syntax tree for MiniDB's SQL subset.
//
//   CREATE TABLE t (col TYPE, ..., PRIMARY KEY (col))
//   INSERT INTO t VALUES (v, ...)
//   SELECT a, b | *  FROM t [JOIN u ON t.x = u.y] [WHERE preds]
//   DELETE FROM t [WHERE preds]
//   BEGIN | COMMIT | ABORT
//
// WHERE is a conjunction (AND) of simple comparisons. Each side of a comparison
// is either a column reference or a constant.
// ---------------------------------------------------------------------------

enum class CompOp { EQ, NE, LT, LE, GT, GE };

// A possibly table-qualified column reference, e.g. "users.id" or just "id".
struct ColumnRef {
    std::string table;  // empty when unqualified
    std::string column;
};

// One side of a comparison: a column or a literal constant.
struct Operand {
    bool      is_column{false};
    ColumnRef col;
    Value     constant;
};

struct Predicate {
    Operand left;
    CompOp  op;
    Operand right;
};

enum class StmtType { CREATE_TABLE, INSERT, SELECT, DELETE, BEGIN, COMMIT, ABORT };

// Base statement.
struct Statement {
    StmtType type;
    virtual ~Statement() = default;
};

struct CreateTableStmt : Statement {
    CreateTableStmt() { type = StmtType::CREATE_TABLE; }
    std::string name;
    Schema      schema;
};

struct InsertStmt : Statement {
    InsertStmt() { type = StmtType::INSERT; }
    std::string        table;
    std::vector<Value> values;
};

struct JoinClause {
    bool        present{false};
    std::string table;     // right-hand table
    ColumnRef   left;      // join column on the left table
    ColumnRef   right;     // join column on the right table
};

struct SelectStmt : Statement {
    SelectStmt() { type = StmtType::SELECT; }
    bool                     select_star{false};
    std::vector<ColumnRef>   columns;    // projection list (when not '*')
    std::string              from_table;
    JoinClause               join;
    std::vector<Predicate>   where;      // AND-connected
};

struct DeleteStmt : Statement {
    DeleteStmt() { type = StmtType::DELETE; }
    std::string            table;
    std::vector<Predicate> where;
};

struct TxnStmt : Statement {
    explicit TxnStmt(StmtType t) { type = t; } // BEGIN / COMMIT / ABORT
};

} // namespace minidb
