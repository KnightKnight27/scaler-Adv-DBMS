#pragma once

#include "parser/ast.h"
#include "parser/lexer.h"

#include <vector>

namespace minidb {

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    Statement Parse();

private:
    Token& Current();
    Token Consume(TokenType expected);
    bool Match(TokenType type) const;

    SelectStatement ParseSelect();
    InsertStatement ParseInsert();
    DeleteStatement ParseDelete();

    std::unique_ptr<Expr> ParseExpression();
    std::unique_ptr<Expr> ParseAndExpression();
    std::unique_ptr<Expr> ParsePrimary();
    std::unique_ptr<Expr> ParseCondition();
    Value ParseValue();

    std::vector<Token> tokens_;
    std::size_t pos_ = 0;
};

}  // namespace minidb
