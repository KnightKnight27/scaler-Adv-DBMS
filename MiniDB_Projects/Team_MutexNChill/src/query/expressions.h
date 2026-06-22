#pragma once
#include <string>
#include <memory>

// Base class for all expression nodes in the AST.
struct Expr {
    virtual ~Expr() = default;
};

// A plain integer, e.g. the 25 in "age > 25".
struct NumberExpr : Expr {
    int value;
    explicit NumberExpr(int v) : value(v) {}
};

// A column reference, e.g. "age" or "students.age" (table prefix optional).
struct ColumnExpr : Expr {
    std::string table;  // empty if no table prefix
    std::string column;
    ColumnExpr(std::string t, std::string c) : table(t), column(c) {}
};

// A comparison: left op right, where op is "=", "!=", ">", "<", ">=", "<=".
struct CompareExpr : Expr {
    std::string op;
    Expr* left;
    Expr* right;
    CompareExpr(std::string o, Expr* l, Expr* r) : op(o), left(l), right(r) {}
    ~CompareExpr() { delete left; delete right; }
};

// AND or OR of two sub-expressions.
struct LogicExpr : Expr {
    std::string op; // "AND" or "OR"
    Expr* left;
    Expr* right;
    LogicExpr(std::string o, Expr* l, Expr* r) : op(o), left(l), right(r) {}
    ~LogicExpr() { delete left; delete right; }
};
