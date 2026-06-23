// Abstract syntax tree for the SQL subset MiniDB understands.
//
// Expressions form a small tree (column refs, literals, comparisons, AND/OR).
// Statements cover CREATE TABLE / INSERT / SELECT (WHERE, JOIN, GROUP BY,
// ORDER BY, aggregates) / DELETE / UPDATE / transaction control.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "catalog/schema.hpp"
#include "common/types.hpp"

namespace minidb {

// ---- expressions ----------------------------------------------------------
enum class ExprKind { COLUMN, LITERAL, BINARY };
enum class BinOp { EQ, NE, LT, LE, GT, GE, AND, OR };

struct Expr {
    ExprKind kind;

    // COLUMN
    std::string table;   // optional qualifier (empty if unqualified)
    std::string column;

    // LITERAL
    Value literal;

    // BINARY
    BinOp op{};
    std::shared_ptr<Expr> left, right;

    static std::shared_ptr<Expr> Column(std::string tbl, std::string col) {
        auto e = std::make_shared<Expr>();
        e->kind = ExprKind::COLUMN; e->table = std::move(tbl); e->column = std::move(col);
        return e;
    }
    static std::shared_ptr<Expr> Literal(Value v) {
        auto e = std::make_shared<Expr>();
        e->kind = ExprKind::LITERAL; e->literal = std::move(v);
        return e;
    }
    static std::shared_ptr<Expr> Binary(BinOp op, std::shared_ptr<Expr> l,
                                        std::shared_ptr<Expr> r) {
        auto e = std::make_shared<Expr>();
        e->kind = ExprKind::BINARY; e->op = op; e->left = std::move(l); e->right = std::move(r);
        return e;
    }
};
using ExprPtr = std::shared_ptr<Expr>;

// ---- statements -----------------------------------------------------------
enum class StmtKind {
    CREATE_TABLE, INSERT, SELECT, DELETE, UPDATE,
    BEGIN, COMMIT, ABORT
};

struct Statement { virtual ~Statement() = default; StmtKind kind; };

struct CreateTableStmt : Statement {
    CreateTableStmt() { kind = StmtKind::CREATE_TABLE; }
    std::string table;
    std::vector<Column> columns;
};

struct InsertStmt : Statement {
    InsertStmt() { kind = StmtKind::INSERT; }
    std::string table;
    std::vector<std::string> columns;        // empty => all columns in order
    std::vector<std::vector<Value>> rows;    // one or more VALUES tuples
};

enum class AggFunc { NONE, COUNT, SUM, MIN, MAX, AVG };

struct SelectItem {
    bool is_star = false;            // SELECT *
    AggFunc agg = AggFunc::NONE;     // aggregate function (NONE for plain column)
    bool agg_star = false;           // COUNT(*)
    std::string table;               // optional qualifier
    std::string column;              // column name (or aggregate argument)
    std::string alias;               // output name
};

struct TableRef { std::string name; std::string alias; };

struct JoinClause {
    TableRef table;
    ExprPtr  on;     // join predicate
};

struct OrderBy { std::string table; std::string column; bool desc = false; };

struct SelectStmt : Statement {
    SelectStmt() { kind = StmtKind::SELECT; }
    std::vector<SelectItem> items;
    TableRef from;
    std::vector<JoinClause> joins;
    ExprPtr where;                          // may be null
    std::vector<std::pair<std::string, std::string>> group_by;  // (table, column)
    bool has_order_by = false;
    OrderBy order_by;
};

struct DeleteStmt : Statement {
    DeleteStmt() { kind = StmtKind::DELETE; }
    std::string table;
    ExprPtr where;   // may be null (delete all)
};

struct UpdateStmt : Statement {
    UpdateStmt() { kind = StmtKind::UPDATE; }
    std::string table;
    std::vector<std::pair<std::string, Value>> assignments;
    ExprPtr where;   // may be null
};

struct TxnStmt : Statement { explicit TxnStmt(StmtKind k) { kind = k; } };

}  // namespace minidb
