#pragma once

#include "parser/tokenizer.h"
#include "common/types.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

// ============================================================
// AST Nodes — output of the parser
// ============================================================

// ---- Expression tree (for WHERE clauses) ----
struct Expr {
    enum Type { LITERAL, COLUMN_REF, COMPARE, AND_EXPR, OR_EXPR };
    Type type;

    // LITERAL
    Value value;

    // COLUMN_REF
    std::string table_name;   // optional qualifier (t1.col)
    std::string column_name;

    // COMPARE: op is "=", "!=", "<", ">", "<=", ">="
    std::string op;

    // Binary children (COMPARE, AND, OR)
    std::shared_ptr<Expr> left;
    std::shared_ptr<Expr> right;

    // Factory helpers
    static std::shared_ptr<Expr> MakeLiteral(const Value& v);
    static std::shared_ptr<Expr> MakeColumnRef(const std::string& table,
                                                const std::string& col);
    static std::shared_ptr<Expr> MakeCompare(const std::string& op,
                                              std::shared_ptr<Expr> l,
                                              std::shared_ptr<Expr> r);
    static std::shared_ptr<Expr> MakeAnd(std::shared_ptr<Expr> l,
                                          std::shared_ptr<Expr> r);
};

using ExprPtr = std::shared_ptr<Expr>;

// ---- Column reference with optional table prefix ----
struct ColumnRef {
    std::string table;
    std::string column;
};

// ---- JOIN clause ----
struct JoinClause {
    std::string table_name;
    ColumnRef left_col;
    ColumnRef right_col;
};

// ---- Statement types ----
enum class StmtType { CREATE_TABLE, INSERT, SELECT, DELETE_STMT, UPDATE };

struct CreateTableStmt {
    std::string table_name;
    std::vector<Column> columns;
    int pk_index = -1;
};

struct InsertStmt {
    std::string table_name;
    std::vector<Value> values;
};

struct SelectStmt {
    bool select_all = false;
    std::vector<ColumnRef> columns;
    std::string table_name;
    std::optional<JoinClause> join;
    ExprPtr where_clause;
};

struct DeleteStmt {
    std::string table_name;
    ExprPtr where_clause;
};

struct UpdateStmt {
    std::string table_name;
    std::vector<std::pair<std::string, Value>> assignments;
    ExprPtr where_clause;
};

// ---- Top-level parsed statement ----
struct Statement {
    StmtType type;
    CreateTableStmt create_table;
    InsertStmt insert;
    SelectStmt select;
    DeleteStmt delete_stmt;
    UpdateStmt update;
};

// ============================================================
// Parser — recursive-descent, produces a Statement from tokens
// ============================================================

class Parser {
public:
    explicit Parser(const std::vector<Token>& tokens);

    // Parse one statement. Throws std::runtime_error on syntax error.
    Statement Parse();

private:
    std::vector<Token> tokens_;
    int pos_;

    // Token helpers
    const Token& Current() const;
    const Token& Peek() const;
    Token Consume(TokenType expected, const std::string& err_msg);
    bool Match(TokenType type);
    bool Check(TokenType type) const;

    // Statement parsers
    Statement ParseCreateTable();
    Statement ParseInsert();
    Statement ParseSelect();
    Statement ParseDelete();
    Statement ParseUpdate();

    // Expression parser (WHERE clause)
    ExprPtr ParseExpr();          // handles OR
    ExprPtr ParseAndExpr();       // handles AND
    ExprPtr ParseComparison();    // handles col op value
    ExprPtr ParsePrimary();       // literal or column_ref

    // Helpers
    Value ParseLiteralValue();
    ColumnRef ParseColumnRef();
    DataType ParseDataType(int& max_len);
};
