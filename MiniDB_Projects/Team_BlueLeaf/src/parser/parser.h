#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "parser/ast.h"
#include "parser/token.h"

namespace minidb {

// Recursive-descent parser producing one Statement AST from a token stream.
// Operator precedence is encoded by the call hierarchy: OR < AND < comparison.
class Parser {
public:
    explicit Parser(std::vector<Token> toks) : toks_(std::move(toks)) {}

    StmtPtr parse();  // parse a single statement (optionally ending in ';')

    // True when no statements remain (used to parse a multi-statement script).
    bool at_end() const { return toks_[pos_].type == TokenType::END; }

private:
    StmtPtr parse_create();
    StmtPtr parse_insert();
    StmtPtr parse_delete();
    StmtPtr parse_select();

    void        parse_select_list(SelectStmt& stmt);
    std::string parse_column_name();           // IDENTIFIER ['.' IDENTIFIER]
    Value       parse_value();                 // NUMBER | STRING literal

    // expression grammar
    ExprPtr parse_expr();
    ExprPtr parse_or();
    ExprPtr parse_and();
    ExprPtr parse_comparison();
    ExprPtr parse_primary();

    // token helpers
    const Token& peek() const { return toks_[pos_]; }
    const Token& advance() { return toks_[pos_++]; }
    bool check(TokenType t) const { return peek().type == t; }
    bool match(TokenType t) { if (check(t)) { ++pos_; return true; } return false; }
    const Token& expect(TokenType t, const char* what);

    std::vector<Token> toks_;
    std::size_t        pos_ = 0;
};

} // namespace minidb
