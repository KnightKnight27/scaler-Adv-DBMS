#pragma once

#include <memory>
#include <string>
#include <vector>

#include "catalog/schema.h"
#include "common/types.h"

namespace minidb {

// ---------- expressions (WHERE / JOIN ... ON predicates) ----------
enum class ExprKind { Literal, Column, Binary };

struct Expr {
    virtual ~Expr() = default;
    virtual ExprKind kind() const = 0;
};
using ExprPtr = std::unique_ptr<Expr>;

struct LiteralExpr : Expr {
    Value value;
    explicit LiteralExpr(Value v) : value(std::move(v)) {}
    ExprKind kind() const override { return ExprKind::Literal; }
};

struct ColumnExpr : Expr {
    std::string table;  // optional qualifier (for joins); empty if unqualified
    std::string name;
    ColumnExpr(std::string t, std::string n) : table(std::move(t)), name(std::move(n)) {}
    ExprKind kind() const override { return ExprKind::Column; }
};

struct BinaryExpr : Expr {
    std::string op;  // "=", "!=", "<", ">", "<=", ">=", "AND", "OR"
    ExprPtr left, right;
    BinaryExpr(std::string o, ExprPtr l, ExprPtr r)
        : op(std::move(o)), left(std::move(l)), right(std::move(r)) {}
    ExprKind kind() const override { return ExprKind::Binary; }
};

// ---------- statements ----------
enum class StmtKind { CreateTable, Insert, Delete, Select };

struct Statement {
    virtual ~Statement() = default;
    virtual StmtKind kind() const = 0;
};
using StmtPtr = std::unique_ptr<Statement>;

struct CreateTableStmt : Statement {
    std::string         table;
    std::vector<Column> columns;
    int                 primary_key = -1;  // index into columns, or -1
    StmtKind kind() const override { return StmtKind::CreateTable; }
};

struct InsertStmt : Statement {
    std::string                      table;
    std::vector<std::vector<Value>>  rows;
    StmtKind kind() const override { return StmtKind::Insert; }
};

struct DeleteStmt : Statement {
    std::string table;
    ExprPtr     where;  // may be null
    StmtKind kind() const override { return StmtKind::Delete; }
};

struct AggCall {
    std::string func;    // COUNT, SUM, AVG, MIN, MAX
    std::string column;  // "*" for COUNT(*)
};

struct SelectStmt : Statement {
    std::vector<std::string> columns;     // projected columns; {"*"} means all
    std::vector<AggCall>     aggregates;
    std::string              from_table;
    std::string              from_alias;
    std::string              join_table;  // empty if no join
    std::string              join_alias;
    ExprPtr                  join_on;      // may be null
    ExprPtr                  where;        // may be null
    std::vector<std::string> group_by;
    StmtKind kind() const override { return StmtKind::Select; }
};

} // namespace minidb
