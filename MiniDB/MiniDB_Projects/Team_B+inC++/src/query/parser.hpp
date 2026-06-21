#pragma once

#include <cstddef>
#include <vector>

#include "ast.hpp"
#include "token.hpp"

// Recursive-descent parser: token stream -> one Statement (AST).
// Expression precedence (loosest to tightest): OR < AND < comparison < primary.
class Parser {
public:
    explicit Parser(std::vector<Token> toks) : toks_(std::move(toks)) {}
    Statement parse();

private:
    std::vector<Token> toks_;
    std::size_t        pos_ = 0;

    const Token& peek() const { return toks_[pos_]; }
    const Token& advance() { return toks_[pos_++]; }
    bool check(Tok t) const { return peek().type == t; }
    bool match(Tok t) { if (check(t)) { ++pos_; return true; } return false; }
    const Token& expect(Tok t, const char* what);

    CreateStmt parse_create();
    InsertStmt parse_insert();
    SelectStmt parse_select();
    DeleteStmt parse_delete();

    void  read_colref(std::string& table, std::string& name);  // IDENT [. IDENT]
    Value parse_literal_value();

    std::unique_ptr<Expr> parse_or();
    std::unique_ptr<Expr> parse_and();
    std::unique_ptr<Expr> parse_cmp();
    std::unique_ptr<Expr> parse_primary();
};
