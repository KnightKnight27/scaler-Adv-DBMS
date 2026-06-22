#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Row.h"
#include "Value.h"

namespace sql {

// WHERE-clause predicate hierarchy. Every node answers the same question
// for a given row, so nodes are fully interchangeable through the base
// pointer (Liskov), and the query engine depends only on this interface
// rather than on any concrete node type (Dependency Inversion).
struct BoolExpr {
    virtual ~BoolExpr() = default;
    virtual bool eval(const Row& row) const = 0;
};

using BoolExprPtr = std::unique_ptr<BoolExpr>;

enum class CompOp { Eq, Ne, Lt, Le, Gt, Ge };

// column <op> literal
class Comparison : public BoolExpr {
public:
    Comparison(std::string column, CompOp op, Value literal)
        : column_(std::move(column)), op_(op), literal_(std::move(literal)) {}

    bool eval(const Row& row) const override;

private:
    std::string column_;
    CompOp op_;
    Value literal_;
};

enum class LogicOp { And, Or };

// lhs AND/OR rhs
class Logical : public BoolExpr {
public:
    Logical(LogicOp op, BoolExprPtr lhs, BoolExprPtr rhs)
        : op_(op), lhs_(std::move(lhs)), rhs_(std::move(rhs)) {}

    bool eval(const Row& row) const override;

private:
    LogicOp op_;
    BoolExprPtr lhs_;
    BoolExprPtr rhs_;
};

// Parsed form of: SELECT <columns> FROM <table> [WHERE <predicate>]
struct SelectStatement {
    bool selectAll = false;             // true when the projection is '*'
    std::vector<std::string> columns;   // projected columns when !selectAll
    std::string table;                  // table name (informational)
    BoolExprPtr where;                  // null when there is no WHERE clause
};

}  // namespace sql
