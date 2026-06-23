#pragma once

#include <string>
#include <vector>

#include "parser/token.h"

namespace minidb {

// Turns a SQL string into a token stream. Keywords are case-insensitive;
// identifiers, integer/decimal numbers, and single-quoted string literals are
// recognised, along with the comparison and punctuation operators MiniDB uses.
class Lexer {
public:
    explicit Lexer(std::string sql) : sql_(std::move(sql)) {}
    std::vector<Token> tokenize();

private:
    std::string sql_;
};

} // namespace minidb
