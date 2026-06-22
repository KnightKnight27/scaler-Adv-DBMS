// ============================================================================
//  ast.hpp — Abstract Syntax Tree for the MiniDB SQL subset.
//
//  The parser turns a query string into one of these Statement structs. The
//  executor (M3) then walks the struct — it never sees raw text. Keeping the
//  AST as plain data (no behaviour) is what lets the optimizer rewrite a plan
//  without touching the parser.
//
//  Supported grammar (informally):
//    CREATE TABLE t (col TYPE, ...)
//    INSERT INTO t [(cols)] VALUES (v, ...)
//    SELECT cols FROM t [JOIN u ON a.x = b.y] [WHERE pred]
//    DELETE FROM t [WHERE pred]
//  where pred is a boolean combination (AND/OR) of   col OP literal   terms.
// ============================================================================
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace minidb {

enum class ColType { INT, TEXT };

// A literal value flowing through the parser/executor. Tagged union kept simple
// as a struct with both fields; `is_null` marks SQL NULL.
struct Value {
    ColType type = ColType::INT;
    int64_t i = 0;
    std::string s;
    bool is_null = false;

    static Value Int(int64_t v)            { Value x; x.type = ColType::INT;  x.i = v; return x; }
    static Value Text(std::string v)       { Value x; x.type = ColType::TEXT; x.s = std::move(v); return x; }
};

enum class CmpOp { EQ, NE, LT, LE, GT, GE };

// A WHERE predicate is a small expression tree: leaves are comparisons,
// internal nodes are AND/OR. Recursive structure mirrors the grammar.
struct Expr {
    enum class Kind { Compare, And, Or } kind;

    // Compare: `column OP literal`  (column may be "table.col" or "col")
    std::string column;
    CmpOp op = CmpOp::EQ;
    Value literal;

    // And/Or: two children
    std::unique_ptr<Expr> lhs, rhs;

    static std::unique_ptr<Expr> Cmp(std::string col, CmpOp o, Value v) {
        auto e = std::make_unique<Expr>();
        e->kind = Kind::Compare; e->column = std::move(col); e->op = o; e->literal = std::move(v);
        return e;
    }
    static std::unique_ptr<Expr> Logic(Kind k, std::unique_ptr<Expr> a, std::unique_ptr<Expr> b) {
        auto e = std::make_unique<Expr>();
        e->kind = k; e->lhs = std::move(a); e->rhs = std::move(b);
        return e;
    }
};

// ---- Statement variants ----------------------------------------------------
struct ColumnDef { std::string name; ColType type; };

struct CreateStmt {
    std::string table;
    std::vector<ColumnDef> columns;
};

struct InsertStmt {
    std::string table;
    std::vector<std::string> columns;   // empty => all columns, in table order
    std::vector<Value> values;
};

struct JoinClause {
    bool present = false;
    std::string table;        // the joined (right) table
    std::string left_col;     // a.x   in  ON a.x = b.y
    std::string right_col;    // b.y
};

struct SelectStmt {
    std::vector<std::string> columns;   // "*" represented as single "*"
    std::string table;                  // primary (left) table
    JoinClause join;
    std::unique_ptr<Expr> where;        // nullptr => no filter
};

struct DeleteStmt {
    std::string table;
    std::unique_ptr<Expr> where;
};

// A parsed statement is exactly one of the above.
struct Statement {
    enum class Kind { Create, Insert, Select, Delete } kind;
    CreateStmt create;
    InsertStmt insert;
    SelectStmt select;
    DeleteStmt del;
};

}  // namespace minidb
