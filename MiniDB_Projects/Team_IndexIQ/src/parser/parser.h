#pragma once
#include "parser/types.h"
#include "parser/ast.h"
#include <vector>

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);
    Statement parse();

private:
    std::vector<Token> tokens_;
    size_t pos_ = 0;

    Token& cur()  { return tokens_[pos_]; }
    Token  consume(TokenType expected);
    bool   check(TokenType t) const { return tokens_[pos_].type == t; }
    bool   match(TokenType t) { if (check(t)) { pos_++; return true; } return false; }

    Statement parse_select(bool explain);
    Statement parse_insert();
    Statement parse_delete();
    Statement parse_create();

    Expression* parse_expr();
    Expression* parse_and();
    Expression* parse_comparison();
    Expression* parse_primary();
};
