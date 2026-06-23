#pragma once

#include "types.h"
#include "select_stat.h"
#include <vector>

class DbParser {
public:
    explicit DbParser(std::vector<Token> tokens);
    SelectStatement parseSelect();

private:
    std::vector<Token> tokens;
    size_t pos = 0;

    Token peek() const;
    Token advance();
    bool match(TokenType type);
    Token consume(TokenType type, const std::string& errorMsg);
    bool isAtEnd() const;

    Expression* parseExpression();
    Expression* parseOr();
    Expression* parseAnd();
    Expression* parseComparison();
    Expression* parsePrimary();
};
