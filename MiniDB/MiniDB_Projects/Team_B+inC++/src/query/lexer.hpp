#pragma once

#include <string>
#include <vector>

#include "token.hpp"

// Turns a SQL string into a flat token stream ending in a Tok::END sentinel.
// Keywords are matched case-insensitively; identifiers keep their original case.
class Lexer {
public:
    explicit Lexer(std::string sql) : sql_(std::move(sql)) {}
    std::vector<Token> tokenize();

private:
    std::string sql_;
    std::size_t pos_ = 0;
};
