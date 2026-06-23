#pragma once

#include <string>
#include <vector>
#include <memory>
#include "catalog/schema.h"
#include "parser/expression.h"

namespace minidb {

// The kind of a parsed statement. The parser produces exactly one Statement
// subclass per input; the planner / REPL dispatches on `type`.
enum class StatementType {
    CREATE_TABLE,
    INSERT,
    SELECT,
    DELETE,
    CREATE_INDEX,
    BEGIN_TXN,
    COMMIT_TXN,
    ROLLBACK_TXN,
    EXIT,
    INVALID
};

// One column definition in a CREATE TABLE (name + resolved type + length).
struct ColumnDefinition {
    std::string name;
    TypeId type;
    uint32_t length;
};

// A (possibly table-qualified) column reference in a SELECT list or JOIN key,
// e.g. `name` or `orders.user_id`.
struct SelectColumn {
    std::string table;   // empty if unqualified
    std::string column;
};

// An equi-join clause: FROM <left> JOIN <right_table> ON <left_key> = <right_key>.
struct JoinClause {
    bool present{false};
    std::string right_table;
    SelectColumn left_key;
    SelectColumn right_key;
};

// ---------------------------------------------------------------------------
// Statement AST
// ---------------------------------------------------------------------------
struct Statement {
    StatementType type;
    explicit Statement(StatementType t) : type(t) {}
    virtual ~Statement() = default;
};

using StmtPtr = std::unique_ptr<Statement>;

// Produced on any parse error; carries a human-readable message.
struct InvalidStatement : Statement {
    std::string error;
    explicit InvalidStatement(std::string err)
        : Statement(StatementType::INVALID), error(std::move(err)) {}
};

struct CreateTableStatement : Statement {
    std::string table_name;
    std::vector<ColumnDefinition> columns;
    CreateTableStatement() : Statement(StatementType::CREATE_TABLE) {}
};

struct InsertStatement : Statement {
    std::string table_name;
    // Supports multi-row inserts: VALUES (..), (..), ..
    std::vector<std::vector<std::string>> rows;
    InsertStatement() : Statement(StatementType::INSERT) {}
};

struct SelectStatement : Statement {
    bool select_star{true};
    std::vector<SelectColumn> select_list; // populated only when select_star == false
    std::string table_name;                // the FROM (left) table
    JoinClause join;
    ExprPtr where;                         // optional predicate, may be null
    SelectStatement() : Statement(StatementType::SELECT) {}
};

struct DeleteStatement : Statement {
    std::string table_name;
    ExprPtr where; // optional; null means DELETE every row
    DeleteStatement() : Statement(StatementType::DELETE) {}
};

struct CreateIndexStatement : Statement {
    std::string index_name;
    std::string table_name;
    std::string column_name;
    CreateIndexStatement() : Statement(StatementType::CREATE_INDEX) {}
};

// BEGIN / COMMIT / ROLLBACK / EXIT carry no payload beyond their type.
struct SimpleStatement : Statement {
    explicit SimpleStatement(StatementType t) : Statement(t) {}
};

} // namespace minidb
