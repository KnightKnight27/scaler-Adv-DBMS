#pragma once
#include "types.h"
#include <string>
#include <vector>

// The Lexer splits a SQL string into a list of Tokens.
class Lexer {
public:
    explicit Lexer(const std::string& sql);
    std::vector<Token> tokenize();

private:
    std::string input;
};
