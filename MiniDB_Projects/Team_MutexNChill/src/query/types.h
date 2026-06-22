#pragma once
#include <string>

// All token types the lexer can produce from a SQL string.
enum class TokenType {
    // Keywords
    SELECT, FROM, WHERE, INSERT, INTO, VALUES,
    DELETE, JOIN, ON, AND, OR,
    // Identifiers (table names, column names) and literals
    IDENTIFIER, NUMBER, STRING,
    // Comparison operators
    EQ,   // =
    NEQ,  // !=
    GT,   // >
    LT,   // <
    GTE,  // >=
    LTE,  // <=
    // Punctuation
    LPAREN,    // (
    RPAREN,    // )
    COMMA,     // ,
    STAR,      // *
    DOT,       // .
    // End of input
    END
};

struct Token {
    TokenType   type;
    std::string text;
};
