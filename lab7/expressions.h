#pragma once

#include <string>
#include <memory>

// Minimal AST nodes for expressions.
struct Expression {
    virtual ~Expression() = default;
};

using ExprPtr = std::shared_ptr<Expression>;

struct Literal : Expression {
    double value;
    explicit Literal(double v) : value(v) {}
};

struct StringLiteral : Expression {
    std::string value;
    explicit StringLiteral(const std::string& v) : value(v) {}
};

struct ColumnRef : Expression {
    std::string name;
    explicit ColumnRef(const std::string& n) : name(n) {}
};

struct BinaryExpression : Expression {
    std::string op;
    ExprPtr left;
    ExprPtr right;

    BinaryExpression(const std::string& o, ExprPtr l, ExprPtr r)
        : op(o), left(l), right(r) {}
};
