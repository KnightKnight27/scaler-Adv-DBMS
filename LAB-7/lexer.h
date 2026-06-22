#pragma once
#include "types.h"
#include <string>
#include <vector>

class Lexer {
public:
    explicit Lexer(std::string sql);
    std::vector<Token> tokenize();

private:
    std::string input;
};
