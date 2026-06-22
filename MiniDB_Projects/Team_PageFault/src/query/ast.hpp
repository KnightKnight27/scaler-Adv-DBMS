#pragma once
#include <string>
#include <vector>
#include <memory>
#include "../common/value.hpp"
#include "../catalog/schema.hpp"

namespace minidb {

// A WHERE expression: either a comparison (col OP literal) or a boolean
// combination of two sub-expressions. Precedence (OR < AND < comparison) is
// fixed by how the parser builds the tree.
struct Expr {
    enum class Kind { CMP, AND, OR } kind;
    enum class Op { EQ, NE, LT, LE, GT, GE } op = Op::EQ;

    std::string col;   // for CMP: the (possibly table-qualified) column name
    Value val;         // for CMP: the literal to compare against

    std::unique_ptr<Expr> left;   // for AND/OR
    std::unique_ptr<Expr> right;  // for AND/OR
};

struct CreateStmt {
    std::string table;
    std::vector<Column> cols;
};

struct InsertStmt {
    std::string table;
    std::vector<Value> values;
};

struct JoinClause {
    bool present = false;
    std::string table;
    std::string left_col;   // qualified column from the left/base table
    std::string right_col;  // qualified column from the joined table
};

struct SelectStmt {
    bool star = false;            // SELECT *
    bool count_star = false;      // SELECT COUNT(*)
    std::vector<std::string> cols;
    std::string table;
    JoinClause join;
    std::unique_ptr<Expr> where;
};

struct DeleteStmt {
    std::string table;
    std::unique_ptr<Expr> where;
};

// A single parsed statement (move-only, because of the unique_ptr fields).
struct Statement {
    enum class Kind { CREATE, INSERT, SELECT, DELETE, BEGIN, COMMIT, ROLLBACK } kind;
    CreateStmt create;
    InsertStmt insert;
    SelectStmt select;
    DeleteStmt del;
};

} // namespace minidb
