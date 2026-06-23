#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace minidb {

enum class ValueType { INT, STRING };

struct Value {
    ValueType type = ValueType::INT;
    int64_t int_val = 0;
    std::string str_val;
};

struct Expr {
    virtual ~Expr() = default;
};

struct LiteralExpr : Expr {
    explicit LiteralExpr(Value value) : value(std::move(value)) {}
    Value value;
};

struct ColumnRefExpr : Expr {
    explicit ColumnRefExpr(std::string name) : name(std::move(name)) {}
    std::string name;
};

struct BinaryExpr : Expr {
    BinaryExpr(std::string op, std::unique_ptr<Expr> left, std::unique_ptr<Expr> right)
        : op(std::move(op)), left(std::move(left)), right(std::move(right)) {}
    std::string op;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
};

struct JoinClause {
    std::string table;
    std::string left_column;
    std::string right_column;
};

struct SelectStatement {
    std::string column;
    std::string table;
    std::unique_ptr<Expr> where;
    std::unique_ptr<JoinClause> join;
};

struct InsertStatement {
    std::string table;
    std::vector<std::string> columns;
    std::vector<Value> values;
};

struct DeleteStatement {
    std::string table;
    std::unique_ptr<Expr> where;
};

enum class StatementType { SELECT, INSERT, DELETE };

struct Statement {
    StatementType type = StatementType::SELECT;
    SelectStatement select;
    InsertStatement insert;
    DeleteStatement delete_stmt;
};

std::unique_ptr<Expr> CloneExpr(const Expr* expr);

}  // namespace minidb
