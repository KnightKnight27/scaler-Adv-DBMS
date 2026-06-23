#pragma once

#include <string>

namespace minidb {

enum class TokenType {
    // keywords
    SELECT, FROM, WHERE, INSERT, INTO, VALUES, DELETE, CREATE, TABLE,
    JOIN, ON, AND, OR, GROUP, BY,
    COUNT, SUM, AVG, MIN, MAX,
    INT_TYPE, VARCHAR_TYPE, DOUBLE_TYPE, PRIMARY, KEY,
    // literals / identifiers
    IDENTIFIER, NUMBER, STRING,
    // operators / punctuation
    STAR, COMMA, LPAREN, RPAREN, SEMICOLON, DOT,
    EQ, NEQ, LT, GT, LTE, GTE,
    END
};

struct Token {
    TokenType   type;
    std::string text;   // original text (identifier name, number/string literal)
};

} // namespace minidb
