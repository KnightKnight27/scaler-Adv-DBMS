#pragma once

#include <string>

// One lexical token. `text` holds the raw lexeme (the number/string/identifier
// text for those kinds; empty-ish for fixed punctuation).
enum class Tok {
    // keywords
    SELECT, FROM, WHERE, INSERT, INTO, VALUES, DELETE, CREATE, TABLE,
    JOIN, ON, AND, OR, INT_KW, TEXT_KW, PRIMARY, KEY,
    // operands
    IDENT, NUMBER, STRING,
    // punctuation & operators
    LPAREN, RPAREN, COMMA, SEMI, DOT, STAR,
    EQ, NEQ, LT, GT, LE, GE,
    END
};

struct Token {
    Tok         type;
    std::string text;
};
