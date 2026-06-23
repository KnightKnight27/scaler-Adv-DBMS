#pragma once
// sql/parser.h — Recursive descent SQL parser interface.
//
// The Parser consumes a flat token list (from the Lexer) and builds an AST.
// Each SQL statement type has its own parsing method; expressions are parsed
// with a standard precedence-climbing approach (OR < AND < comparison).

#include <memory>
#include <string>
#include <vector>

#include "sql/ast.h"
#include "sql/lexer.h"

namespace minidb {

class Parser {
public:
    /// Construct a parser over an already-tokenized input.
    explicit Parser(const std::vector<Token>& tokens);

    /// Parse a single SQL statement and return its AST root.
    std::unique_ptr<ASTNode> parse();

private:
    std::vector<Token> tokens_;
    size_t pos_;

    // ── Statement parsers ────────────────────────────────────
    std::unique_ptr<SelectStmt>      parse_select();
    std::unique_ptr<InsertStmt>      parse_insert();
    std::unique_ptr<DeleteStmt>      parse_delete();
    std::unique_ptr<CreateTableStmt> parse_create_table();

    // ── Expression parsers (precedence climbing) ─────────────
    std::unique_ptr<ASTNode> parse_expression();    // OR
    std::unique_ptr<ASTNode> parse_and_expr();      // AND
    std::unique_ptr<ASTNode> parse_comparison();    // =, !=, <, >, <=, >=
    std::unique_ptr<ASTNode> parse_primary();       // literals, column refs, (expr)

    // ── Column list for SELECT ───────────────────────────────
    std::vector<std::unique_ptr<ASTNode>> parse_column_list();

    // ── Value parser (for INSERT VALUES) ─────────────────────
    Value parse_value();

    // ── Token-level helpers ──────────────────────────────────
    const Token& peek() const;
    const Token& advance();
    const Token& expect(TokenType t);
    bool match(TokenType t);
    bool is_at_end() const;
};

/// Convenience one-liner: lex + parse a SQL string.
std::unique_ptr<ASTNode> parse_sql(const std::string& sql);

}  // namespace minidb
