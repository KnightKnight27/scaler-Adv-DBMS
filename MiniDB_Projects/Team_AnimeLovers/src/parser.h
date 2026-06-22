#pragma once
#include "value.h"
#include <string>
#include <vector>
#include <variant>
#include <optional>
#include <memory>
#include <stdexcept>

// ─── Tokens ───────────────────────────────────────────────────────────────────
enum class TokenType {
    // Keywords
    SELECT, FROM, WHERE, JOIN, ON, INSERT, INTO, VALUES,
    DELETE, CREATE, TABLE, DROP,
    BEGIN, COMMIT, ROLLBACK,
    AND, OR, NOT,
    INT_KW,    // "INT" keyword (distinct from INT literal)
    VARCHAR_KW,// "VARCHAR" keyword
    PRIMARY, KEY,
    // Literals & identifiers
    INT_LIT,   // 42
    STR_LIT,   // 'hello'
    IDENT,     // table or column name
    // Operators & punctuation
    EQ, NEQ, LT, LTE, GT, GTE,
    COMMA, LPAREN, RPAREN, STAR, SEMI,
    // Sentinel
    END_OF_INPUT,
};

struct Token {
    TokenType   type;
    std::string lexeme;     // raw text
    int64_t     int_val{};  // filled for INT_LIT
};

// ─── AST node types ──────────────────────────────────────────────────────────

// Column definition used by CREATE TABLE
struct ColDef {
    std::string name;
    Type        type;
    bool        primary_key = false;
};

// A condition used in WHERE / JOIN ON clauses.
// We support simple binary expressions: col OP value  or  col OP col
struct Condition {
    std::string  left_col;   // always a column reference
    std::string  left_table; // optional table qualifier
    TokenType    op;         // EQ, NEQ, LT, LTE, GT, GTE
    // Right-hand side: either a literal value or another column reference
    bool         rhs_is_col = false;
    Value        rhs_val;           // used when rhs_is_col==false
    std::string  rhs_col;           // used when rhs_is_col==true
    std::string  rhs_table;         // optional table qualifier for rhs_col
};

// SELECT statement
struct SelectStmt {
    bool                     star = false;        // SELECT *
    std::vector<std::string> columns;             // selected column names
    std::string              table;               // main FROM table
    // JOIN (at most one join for our scope)
    bool                     has_join   = false;
    std::string              join_table;
    Condition                join_cond;
    // WHERE (at most one condition for simplicity)
    bool                     has_where = false;
    Condition                where_cond;
};

// INSERT INTO table VALUES (v1, v2, ...)
struct InsertStmt {
    std::string        table;
    std::vector<Value> values;
};

// DELETE FROM table WHERE col OP val
struct DeleteStmt {
    std::string table;
    bool        has_where = false;
    Condition   where_cond;
};

// CREATE TABLE name (col1 type1 [PRIMARY KEY], ...)
struct CreateStmt {
    std::string        table;
    std::vector<ColDef> cols;
};

// DROP TABLE name
struct DropStmt { std::string table; };

// Transaction control
struct BeginStmt   {};
struct CommitStmt  {};
struct RollbackStmt{};

using Statement = std::variant<
    SelectStmt, InsertStmt, DeleteStmt,
    CreateStmt, DropStmt,
    BeginStmt, CommitStmt, RollbackStmt
>;

// ─── Parser ───────────────────────────────────────────────────────────────────
// Hand-written recursive-descent parser for the SQL subset MiniDB supports.
// ─────────────────────────────────────────────────────────────────────────────
class Parser {
public:
    explicit Parser(const std::string& sql);

    Statement parse();

private:
    std::vector<Token> tokens_;
    size_t             pos_ = 0;

    // Token stream helpers
    Token&       peek();
    Token&       advance();
    Token&       expect(TokenType t, const std::string& msg);
    bool         match(TokenType t);

    // Grammar rules
    Statement    parse_statement();
    SelectStmt   parse_select();
    InsertStmt   parse_insert();
    DeleteStmt   parse_delete();
    CreateStmt   parse_create();
    DropStmt     parse_drop();
    Condition    parse_condition();
    Value        parse_literal();

    static std::vector<Token> tokenize(const std::string& sql);
};
