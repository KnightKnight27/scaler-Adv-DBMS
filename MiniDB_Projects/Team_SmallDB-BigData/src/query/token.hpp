#pragma once

#include <string>

enum class Tok {
    // keywords
    SELECT, FROM, WHERE, INSERT, INTO, VALUES, DELETE, CREATE, TABLE,
    JOIN, ON, AND, OR, INT_KW, TEXT_KW, PRIMARY, KEY,
    BEGIN, COMMIT, ROLLBACK,
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
