#pragma once

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "../catalog/schema.hpp"
#include "../common/value.hpp"

// expr tree for WHERE / JOIN ON
struct Expr {
    virtual ~Expr() = default;
};

struct ColumnRef : Expr {
    std::string table;  // empty if unqualified
    std::string name;
};

struct Literal : Expr {
    Value val;
};

struct BinaryExpr : Expr {
    std::string           op;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
};

struct CreateStmt {
    std::string table;
    Schema      schema;
    int         pk_col;
};

struct InsertStmt {
    std::string        table;
    std::vector<Value> values;  // in column order
};

struct SelectCol {
    std::string table;  // empty if unqualified
    std::string name;
};

struct SelectStmt {
    bool                    star = false;   // SELECT *
    std::vector<SelectCol>  columns;        // used when !star
    std::string             from;

    bool                    has_join = false;
    std::string             join_table;
    std::string             jl_table, jl_col, jr_table, jr_col;

    std::unique_ptr<Expr>   where;          // null if no WHERE
};

struct DeleteStmt {
    std::string           table;
    std::unique_ptr<Expr> where;            // null = delete all
};

struct BeginStmt {};
struct CommitStmt {};
struct RollbackStmt {};

using Statement = std::variant<CreateStmt, InsertStmt, SelectStmt, DeleteStmt,
                               BeginStmt, CommitStmt, RollbackStmt>;
