#pragma once
#include <vector>
#include "token.hpp"
#include "ast.hpp"

namespace minidb {

// Hand-written recursive-descent parser. Consumes the token stream from the
// lexer and produces one Statement. Throws std::runtime_error on a syntax error.
class Parser {
public:
    explicit Parser(std::vector<Token> toks) : toks_(std::move(toks)) {}
    Statement parse();

private:
    std::vector<Token> toks_;
    size_t pos_ = 0;
    int depth_ = 0;                         // expression nesting depth guard

    Statement parseStatement();

    const Token& peek() const { return toks_[pos_]; }
    const Token& get() { return toks_[pos_++]; }
    bool check(Tok t) const { return peek().type == t; }
    bool accept(Tok t) { if (check(t)) { pos_++; return true; } return false; }
    const Token& expect(Tok t, const char* what);

    Statement parseCreate();
    Statement parseInsert();
    Statement parseSelect();
    Statement parseDelete();

    std::string parseColRef();              // ident [ . ident ]
    Value parseLiteral();
    std::unique_ptr<Expr> parseExpr();      // OR level
    std::unique_ptr<Expr> parseAnd();       // AND level
    std::unique_ptr<Expr> parseCmp();       // comparison / parenthesised
};

} // namespace minidb
