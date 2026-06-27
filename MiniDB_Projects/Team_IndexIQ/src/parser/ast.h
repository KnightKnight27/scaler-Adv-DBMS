#pragma once
#include <string>
#include <vector>
#include <variant>
#include <memory>

struct Expression {
    virtual ~Expression() = default;
};

struct Literal : Expression {
    std::string value;
    explicit Literal(std::string v) : value(std::move(v)) {}
};

struct ColumnRef : Expression {
    std::string table;
    std::string col;
    explicit ColumnRef(std::string c) : col(std::move(c)) {}
    ColumnRef(std::string t, std::string c) : table(std::move(t)), col(std::move(c)) {}
};

struct BinaryExpr : Expression {
    std::string op;
    Expression* left;
    Expression* right;
    BinaryExpr(std::string o, Expression* l, Expression* r)
        : op(std::move(o)), left(l), right(r) {}
    ~BinaryExpr() { delete left; delete right; }
};

struct ColDef {
    std::string name;
    std::string type;
};

struct CreateStmt {
    std::string           table;
    std::vector<ColDef>   columns;
};

struct InsertStmt {
    std::string              table;
    std::vector<std::string> values;
};

struct DeleteStmt {
    std::string  table;
    Expression*  where = nullptr;
    DeleteStmt() = default;
    DeleteStmt(DeleteStmt&& o) noexcept : table(std::move(o.table)), where(o.where) { o.where = nullptr; }
    DeleteStmt& operator=(DeleteStmt&&) = delete;
    ~DeleteStmt() { delete where; }
};

struct SelectStmt {
    std::vector<std::string> cols;
    std::string  table;
    std::string  join_table;
    std::string  join_left_col;
    std::string  join_right_col;
    Expression*  where   = nullptr;
    bool         explain = false;
    SelectStmt() = default;
    SelectStmt(SelectStmt&& o) noexcept
        : cols(std::move(o.cols)), table(std::move(o.table))
        , join_table(std::move(o.join_table))
        , join_left_col(std::move(o.join_left_col))
        , join_right_col(std::move(o.join_right_col))
        , where(o.where), explain(o.explain) { o.where = nullptr; }
    SelectStmt& operator=(SelectStmt&&) = delete;
    ~SelectStmt() { delete where; }
};

struct BeginStmt    {};
struct CommitStmt   {};
struct RollbackStmt {};

using Statement = std::variant<
    CreateStmt, InsertStmt, DeleteStmt, SelectStmt,
    BeginStmt, CommitStmt, RollbackStmt
>;
