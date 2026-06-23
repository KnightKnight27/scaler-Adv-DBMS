#pragma once

#include "common/types.h"
#include <string>
#include <vector>
#include <memory>

namespace minidb {

/*
 * Minimal AST types for the SQL parser.
 *
 * Supported SQL:
 *   CREATE TABLE name (col1 TYPE, col2 TYPE, ...)
 *   INSERT INTO table VALUES (v1, v2, ...)
 *   SELECT cols FROM table [JOIN t2 ON ...] [WHERE ...] 
 *   DELETE FROM table [WHERE ...]
 *
 * WHERE supports: =, !=, <, <=, >, >=, AND, OR
 */

// ── Logic operators for combining predicates ────────────────
enum class LogicOp { AND, OR };

// ── Predicate: col op value ─────────────────────────────────
struct Predicate {
    std::string column;
    CmpOp       op;
    Value       value;
};

// ── Where clause: predicates combined with AND or OR ────────
struct WhereClause {
    std::vector<Predicate> preds;
    LogicOp                logic = LogicOp::AND; // how preds are combined

    bool IsEmpty() const { return preds.empty(); }
};

// ── Join condition: t1.col = t2.col ─────────────────────────
struct JoinCondition {
    std::string left_table;
    std::string left_column;
    std::string right_table;
    std::string right_column;
};

// ── Statement types ─────────────────────────────────────────
enum class StmtType { CREATE_TABLE, SELECT, INSERT, DELETE_STMT };

struct Statement {
    StmtType type;
    virtual ~Statement() = default;
};

// ── CREATE TABLE name (col1 TYPE, col2 TYPE, ...) ───────────
struct CreateTableStmt : Statement {
    std::string table_name;
    Schema      columns;   // column definitions

    CreateTableStmt() { type = StmtType::CREATE_TABLE; }
};

// ── SELECT ──────────────────────────────────────────────────
struct SelectStmt : Statement {
    std::vector<std::string> columns;      // "*" or list of column names
    std::string              table_name;   // FROM table
    WhereClause              where_clause; // WHERE conditions

    // For backward compat — direct access to preds
    std::vector<Predicate>&  where_preds_ref() { return where_clause.preds; }

    // JOIN support
    bool                     has_join = false;
    std::string              join_table;
    JoinCondition            join_cond;

    SelectStmt() { type = StmtType::SELECT; }
};

// ── INSERT INTO table VALUES (...) ──────────────────────────
struct InsertStmt : Statement {
    std::string        table_name;
    std::vector<Value> values;

    InsertStmt() { type = StmtType::INSERT; }
};

// ── DELETE FROM table WHERE ... ─────────────────────────────
struct DeleteStmt : Statement {
    std::string table_name;
    WhereClause where_clause;

    DeleteStmt() { type = StmtType::DELETE_STMT; }
};

} // namespace minidb
