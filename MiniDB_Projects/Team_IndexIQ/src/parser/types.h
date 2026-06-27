#pragma once
#include <string>

enum class TokenType {
    SELECT, FROM, WHERE, JOIN, ON,
    INSERT, INTO, VALUES,
    DELETE,
    CREATE, TABLE,
    BEGIN, COMMIT, ROLLBACK,
    EXPLAIN,
    AND, OR,
    EQ, NEQ, GT, LT, GTE, LTE,
    COMMA, STAR, LPAREN, RPAREN, DOT, SEMICOLON,
    IDENTIFIER, NUMBER, STRING,
    END
};

struct Token {
    TokenType   type;
    std::string text;
};
