#pragma once

#include <string>
#include <vector>

#include "token.hpp"

// sql string -> token stream
class Lexer {
public:
    explicit Lexer(std::string sql) : sql_(std::move(sql)) {}
    std::vector<Token> tokenize();

private:
    std::string sql_;
    std::size_t pos_ = 0;
};
