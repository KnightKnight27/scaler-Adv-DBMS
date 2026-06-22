#pragma once
#include <string>
#include <vector>
#include "token.hpp"

namespace minidb {

// Turns a SQL string into a flat vector of tokens. Keywords are recognised
// case-insensitively; everything else is an identifier, number, or string.
class Lexer {
public:
    explicit Lexer(std::string src) : src_(std::move(src)) {}
    std::vector<Token> tokenize();

private:
    std::string src_;
    size_t pos_ = 0;

    char peek() const { return pos_ < src_.size() ? src_[pos_] : '\0'; }
    char get() { return pos_ < src_.size() ? src_[pos_++] : '\0'; }
};

} // namespace minidb
