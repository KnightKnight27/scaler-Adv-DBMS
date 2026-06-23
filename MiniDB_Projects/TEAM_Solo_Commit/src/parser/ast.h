// MiniDB - Abstract Syntax Tree produced by the parser.
// Expressions are a small tagged tree (column ref / constant / binary op) carrying precedence
// in their shape, exactly like the WHERE-clause AST from Lab 5. Statements are a tiny class
// hierarchy the planner dispatches on.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../common/schema.h"
#include "../common/types.h"

namespace minidb {

// ---------- Expressions ----------
enum class ExprType { Column, Const, Binary };

struct Expr {
    ExprType type;
    std::string col_name;            // Column
    Value val;                       // Const
    std::string op;                  // Binary: = < > <= >= != AND OR
    std::unique_ptr<Expr> left, right;

    static std::unique_ptr<Expr> Col(std::string n) {
        auto e = std::make_unique<Expr>();
        e->type = ExprType::Column;
        e->col_name = std::move(n);
        return e;
    }
    static std::unique_ptr<Expr> Const(Value v) {
        auto e = std::make_unique<Expr>();
        e->type = ExprType::Const;
        e->val = std::move(v);
        return e;
    }
    static std::unique_ptr<Expr> Bin(std::string op, std::unique_ptr<Expr> l, std::unique_ptr<Expr> r) {
        auto e = std::make_unique<Expr>();
        e->type = ExprType::Binary;
        e->op = std::move(op);
        e->left = std::move(l);
        e->right = std::move(r);
        return e;
    }
};

// ---------- Statements ----------
struct Statement {
    enum class Kind { CreateTable, CreateIndex, Insert, Select, Delete, Txn };
    explicit Statement(Kind k) : kind(k) {}
    virtual ~Statement() = default;
    Kind kind;
};

struct CreateTableStmt : Statement {
    CreateTableStmt() : Statement(Kind::CreateTable) {}
    std::string table;
    std::vector<Column> columns;
};

struct CreateIndexStmt : Statement {
    CreateIndexStmt() : Statement(Kind::CreateIndex) {}
    std::string table;
    std::string column;
    bool unique = false;
};

struct InsertStmt : Statement {
    InsertStmt() : Statement(Kind::Insert) {}
    std::string table;
    std::vector<std::string> columns;          // empty => all columns in schema order
    std::vector<std::vector<Value>> rows;      // one or more VALUES tuples
};

struct JoinClause {
    bool present = false;
    std::string table;       // right-hand table
    std::string left_col;    // join key on the left table  (qualified or bare)
    std::string right_col;   // join key on the right table
};

struct SelectStmt : Statement {
    SelectStmt() : Statement(Kind::Select) {}
    std::vector<std::string> select_list;  // {"*"} means all columns
    std::string from_table;
    JoinClause join;
    std::unique_ptr<Expr> where;           // nullptr if no WHERE
    bool for_update = false;               // SELECT ... FOR UPDATE -> take X locks on rows read
};

struct DeleteStmt : Statement {
    DeleteStmt() : Statement(Kind::Delete) {}
    std::string table;
    std::unique_ptr<Expr> where;
};

struct TxnStmt : Statement {
    TxnStmt() : Statement(Kind::Txn) {}
    enum class Op { Begin, Commit, Abort } op = Op::Begin;
};

}  // namespace minidb
