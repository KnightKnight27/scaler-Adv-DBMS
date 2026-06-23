// MiniDB - recursive-descent SQL parser (the Lab 5 parser, widened to whole statements).
// Grammar (precedence encoded by the rule nesting, comparisons tightest, then AND, then OR):
//   stmt   := CREATE TABLE ... | CREATE [UNIQUE] INDEX ... | INSERT ... | SELECT ...
//           | DELETE ... | BEGIN | COMMIT | ABORT
//   expr   := orexpr ;  orexpr := andexpr (OR andexpr)* ;  andexpr := cmp (AND cmp)*
//   cmp    := primary (OP primary)? ;  primary := '(' expr ')' | column | literal
#pragma once

#include <memory>
#include <string>

#include "ast.h"
#include "tokenizer.h"

namespace minidb {

class Parser {
public:
    explicit Parser(const std::string& sql) : toks_(Tokenize(sql)) {}

    // Parse a single statement. Throws std::runtime_error on a syntax error.
    std::unique_ptr<Statement> Parse();

private:
    std::unique_ptr<Statement> ParseCreate();
    std::unique_ptr<Statement> ParseInsert();
    std::unique_ptr<Statement> ParseSelect();
    std::unique_ptr<Statement> ParseDelete();

    std::unique_ptr<Expr> ParseExpr();
    std::unique_ptr<Expr> ParseOr();
    std::unique_ptr<Expr> ParseAnd();
    std::unique_ptr<Expr> ParseCmp();
    std::unique_ptr<Expr> ParsePrimary();

    Value ParseLiteral();
    TypeId ParseType();
    std::string ParseColumnRef();

    const Token& Peek() const { return toks_[pos_]; }
    const Token& Next() { return toks_[pos_++]; }
    bool AcceptKw(const std::string& kw);
    bool AcceptPunct(const std::string& p);
    void ExpectKw(const std::string& kw);
    void ExpectPunct(const std::string& p);

    std::vector<Token> toks_;
    size_t pos_ = 0;
};

}  // namespace minidb
