#pragma once
#include "types.h"
#include "expressions.h"
#include <vector>
#include <string>

// ---- AST node types for SQL statements ----

// SELECT col1, col2 FROM table [JOIN other ON expr] [WHERE expr]
struct SelectStmt {
    std::vector<std::string> columns;   // e.g. {"name", "age"} or {"*"}
    std::string table;
    // Optional JOIN
    std::string join_table;             // empty = no join
    std::string join_left_col;          // left side of ON condition
    std::string join_right_col;         // right side of ON condition
    // Optional WHERE
    Expr* where = nullptr;
    ~SelectStmt() { delete where; }
};

// INSERT INTO table VALUES (v1, v2, v3, v4)
// Values map to: id, name, age, extra  (in that order)
struct InsertStmt {
    std::string table;
    int         id;
    std::string name;
    int         age;
    int         extra;
};

// DELETE FROM table WHERE expr
struct DeleteStmt {
    std::string table;
    Expr*       where = nullptr;
    ~DeleteStmt() { delete where; }
};

// Tag so callers know what kind of statement was parsed.
enum class StmtType { SELECT, INSERT, DELETE };

struct ParseResult {
    StmtType type;
    SelectStmt* select = nullptr;
    InsertStmt* insert = nullptr;
    DeleteStmt* del    = nullptr;
    ~ParseResult() { delete select; delete insert; delete del; }
};

// Recursive-descent parser for a small SQL subset.
class Parser {
public:
    explicit Parser(const std::vector<Token>& tokens);
    ParseResult* parse();

private:
    const std::vector<Token>& tokens;
    int pos;

    Token& cur();
    Token& consume(TokenType expected);
    bool   peek(TokenType t);

    ParseResult* parseSelect();
    ParseResult* parseInsert();
    ParseResult* parseDelete();

    // Expression parsing (handles AND / OR / comparisons)
    Expr* parseExpr();
    Expr* parseAnd();
    Expr* parseComparison();
    Expr* parsePrimary();
};
