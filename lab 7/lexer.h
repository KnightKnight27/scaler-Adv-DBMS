#pragma once

#include "types.h"
#include <vector>
#include <string>

class Lexer {
public:
    explicit Lexer(std::string sql);
    std::vector<Token> tokenize();

private:
    std::string input;
    size_t pos = 0;

    char peek() const;
    char advance();
    void skipWhitespace();
    Token scanWord();
    Token scanNumber();
    Token scanString();
};
