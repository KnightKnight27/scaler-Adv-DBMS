#pragma once

#include <string>
#include <memory>

// Base class for all expression nodes
struct Expression {
    virtual ~Expression() = default;
};

using ExprPtr = std::shared_ptr<Expression>;

// Literal value (numeric)
struct Literal : Expression {
    double value;
    explicit Literal(double v) : value(v) {}
};

// String literal
struct StringLiteral : Expression {
    std::string value;
    explicit StringLiteral(const std::string& v) : value(v) {}
};

// Column reference (e.g., "age", "salary")
struct ColumnRef : Expression {
    std::string name;
    explicit ColumnRef(const std::string& n) : name(n) {}
};

// Binary operation (e.g., a + b, age > 25)
struct BinaryExpression : Expression {
    std::string op;
    ExprPtr left;
    ExprPtr right;
    BinaryExpression(const std::string& o, ExprPtr l, ExprPtr r)
        : op(o), left(l), right(r) {}
};
