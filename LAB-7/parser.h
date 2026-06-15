#pragma once
#include "types.h"
#include <string>
#include <vector>

struct OpInfo {
    int precedence;
    bool right_assoc;
};

class SQLParser {
public:
    explicit SQLParser(std::vector<Token> tokens);
    SelectQuery parseSelect();
    static std::vector<RpnToken> toRPN(const std::vector<Token>& infixTokens);

private:
    std::vector<Token> tokens;
    size_t pos = 0;

    Token current();
    Token consume(TokenType expected, const std::string& errMsg);
    bool match(TokenType type);
    void advance();
    bool isAtEnd();
};
