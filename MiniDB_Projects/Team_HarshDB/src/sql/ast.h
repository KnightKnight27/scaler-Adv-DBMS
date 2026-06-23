#pragma once
// ---------------------------------------------------------------------------
// ast.h - the parsed representation of a SQL statement.
//
// The parser turns a SQL string into one of these structs, which the executor
// then runs. WHERE / JOIN conditions are stored as a small expression tree
// (Expr) so the executor can evaluate them against any row, and the optimizer
// can inspect them to decide between a sequential scan and an index scan.
// ---------------------------------------------------------------------------
#include "../common.h"
#include <memory>
#include <string>
#include <vector>

namespace minidb {

struct Expr {
    enum class Kind { Column, IntLit, StrLit, Binary };
    Kind kind;

    // Column reference (optionally qualified table.column).
    std::string table;
    std::string column;

    // Literals.
    int64_t     int_val = 0;
    std::string str_val;

    // Binary node: op in { =, !=, <, >, <=, >=, AND, OR }.
    std::string op;
    std::shared_ptr<Expr> left, right;

    static std::shared_ptr<Expr> col(std::string t, std::string c) {
        auto e = std::make_shared<Expr>(); e->kind = Kind::Column;
        e->table = std::move(t); e->column = std::move(c); return e;
    }
    static std::shared_ptr<Expr> lit(int64_t v) {
        auto e = std::make_shared<Expr>(); e->kind = Kind::IntLit; e->int_val = v; return e;
    }
    static std::shared_ptr<Expr> lit(std::string v) {
        auto e = std::make_shared<Expr>(); e->kind = Kind::StrLit; e->str_val = std::move(v); return e;
    }
    static std::shared_ptr<Expr> bin(std::string op, std::shared_ptr<Expr> l, std::shared_ptr<Expr> r) {
        auto e = std::make_shared<Expr>(); e->kind = Kind::Binary;
        e->op = std::move(op); e->left = std::move(l); e->right = std::move(r); return e;
    }
};
using ExprPtr = std::shared_ptr<Expr>;

enum class StmtType { Create, Insert, Select, Delete, Begin, Commit, Abort, Unknown };

struct CreateStmt { std::string table; Schema schema; };

struct InsertStmt { std::string table; std::vector<Value> values; };

struct SelectStmt {
    std::vector<std::string> columns;     // empty == SELECT *
    std::string table;                    // primary (left) table
    bool        has_join = false;
    std::string join_table;               // right table
    ExprPtr     join_cond;                // ON left.col = right.col
    ExprPtr     where;                    // optional WHERE
    bool        explain = false;          // EXPLAIN prefix -> print the plan
};

struct DeleteStmt { std::string table; ExprPtr where; };

struct Statement {
    StmtType    type = StmtType::Unknown;
    CreateStmt  create;
    InsertStmt  insert;
    SelectStmt  select;
    DeleteStmt  del;
};

} // namespace minidb
