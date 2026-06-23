#pragma once
// sql/ast.h — Abstract Syntax Tree node definitions for the MiniDB SQL subset.
//
// Every SQL statement is parsed into a tree of ASTNode structs.  Expressions
// (column refs, literals, binary ops) and statements (SELECT, INSERT, DELETE,
// CREATE TABLE) each have their own struct that inherits from ASTNode.
// Ownership is managed through std::unique_ptr so the tree cleans up after
// itself.

#include <memory>
#include <string>
#include <vector>

#include "common/types.h"

namespace minidb {

// ─── Node Type Enum ──────────────────────────────────────────
// Used for quick type checks without dynamic_cast.
enum class ASTNodeType {
    SELECT_STMT,
    INSERT_STMT,
    DELETE_STMT,
    CREATE_TABLE_STMT,
    COLUMN_REF,
    LITERAL,
    BINARY_EXPR,
    STAR_EXPR
};

// ─── Base Node ───────────────────────────────────────────────
struct ASTNode {
    ASTNodeType type;
    virtual ~ASTNode() = default;
protected:
    explicit ASTNode(ASTNodeType t) : type(t) {}
};

// ─── Expression Nodes ────────────────────────────────────────

/// A reference to a column, optionally qualified by table name.
/// Example: "students.name" → table_name="students", column_name="name"
/// Example: "name"          → table_name="",         column_name="name"
struct ColumnRef : ASTNode {
    std::string table_name;   // Empty if unqualified.
    std::string column_name;

    ColumnRef(std::string tbl, std::string col)
        : ASTNode(ASTNodeType::COLUMN_REF),
          table_name(std::move(tbl)),
          column_name(std::move(col)) {}
};

/// A literal value (int, double, string, bool, or NULL).
struct Literal : ASTNode {
    Value value;

    explicit Literal(Value v)
        : ASTNode(ASTNodeType::LITERAL), value(std::move(v)) {}
};

/// A binary expression: left OP right.
/// OP is one of: "=", "!=", "<", ">", "<=", ">=", "AND", "OR".
struct BinaryExpr : ASTNode {
    std::unique_ptr<ASTNode> left;
    std::unique_ptr<ASTNode> right;
    std::string op;

    BinaryExpr(std::unique_ptr<ASTNode> l, std::string oper,
               std::unique_ptr<ASTNode> r)
        : ASTNode(ASTNodeType::BINARY_EXPR),
          left(std::move(l)),
          right(std::move(r)),
          op(std::move(oper)) {}
};

/// Represents SELECT * (all columns).
struct StarExpr : ASTNode {
    StarExpr() : ASTNode(ASTNodeType::STAR_EXPR) {}
};

// ─── Join Clause ─────────────────────────────────────────────
// Not an ASTNode—just a plain struct that lives inside SelectStmt.
struct JoinClause {
    std::string table_name;
    std::string alias;
    std::unique_ptr<ASTNode> condition;  // The ON expression.
};

// ─── Statement Nodes ─────────────────────────────────────────

/// SELECT columns FROM table [alias] [JOIN ...] [WHERE ...]
struct SelectStmt : ASTNode {
    std::vector<std::unique_ptr<ASTNode>> columns;  // ColumnRef or StarExpr
    std::string from_table;
    std::string from_alias;
    std::vector<JoinClause> joins;
    std::unique_ptr<ASTNode> where_clause;           // nullptr if no WHERE.

    SelectStmt() : ASTNode(ASTNodeType::SELECT_STMT) {}
};

/// INSERT INTO table [(cols)] VALUES (vals), (vals), ...
struct InsertStmt : ASTNode {
    std::string table_name;
    std::vector<std::string> column_names;   // Empty = all columns.
    std::vector<std::vector<Value>> rows;    // Multi-row insert support.

    InsertStmt() : ASTNode(ASTNodeType::INSERT_STMT) {}
};

/// DELETE FROM table [WHERE ...]
struct DeleteStmt : ASTNode {
    std::string table_name;
    std::unique_ptr<ASTNode> where_clause;   // nullptr = delete every row.

    DeleteStmt() : ASTNode(ASTNodeType::DELETE_STMT) {}
};

/// CREATE TABLE name (col_def, ..., PRIMARY KEY (col))
struct CreateTableStmt : ASTNode {
    std::string table_name;
    std::vector<Column> columns;             // Uses minidb::Column from types.h.
    std::string primary_key;                 // Column name, empty if none.

    CreateTableStmt() : ASTNode(ASTNodeType::CREATE_TABLE_STMT) {}
};

}  // namespace minidb
