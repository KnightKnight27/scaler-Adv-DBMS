#pragma once

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "../catalog/schema.hpp"
#include "../common/value.hpp"

// ── Expression tree (used in WHERE and JOIN ON) ────────────────────────────
// A WHERE predicate is a tree of comparisons (=, !=, <, >, <=, >=) joined by
// AND / OR. Leaves are column references or literals.

struct Expr {
    virtual ~Expr() = default;
};

// A column reference, optionally qualified by table ("emp.id" -> table="emp").
struct ColumnRef : Expr {
    std::string table;  // empty if unqualified
    std::string name;
};

struct Literal : Expr {
    Value val;
};

// Binary node: op is one of "=","!=","<",">","<=",">=","AND","OR".
struct BinaryExpr : Expr {
    std::string           op;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
};

// ── Statements ─────────────────────────────────────────────────────────────

struct CreateStmt {
    std::string table;
    Schema      schema;
    int         pk_col;
};

struct InsertStmt {
    std::string        table;
    std::vector<Value> values;  // in column order
};

// One projected column in a SELECT list (e.g. "name" or "dept.id").
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
    // equi-join condition: <jl_table>.<jl_col> = <jr_table>.<jr_col>
    std::string             jl_table, jl_col, jr_table, jr_col;

    std::unique_ptr<Expr>   where;          // null if no WHERE
};

struct DeleteStmt {
    std::string           table;
    std::unique_ptr<Expr> where;            // null = delete all
};

// Transaction-control statements (no fields).
struct BeginStmt {};
struct CommitStmt {};
struct RollbackStmt {};

// A parsed statement is exactly one of these.
using Statement = std::variant<CreateStmt, InsertStmt, SelectStmt, DeleteStmt,
                               BeginStmt, CommitStmt, RollbackStmt>;
