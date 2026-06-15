#pragma once

#include <string>
#include <variant>
#include <unordered_map>

// A cell can hold either a number or a string.
using Value = std::variant<double, std::string>;

// One row of in-memory data.
struct Row {
    std::unordered_map<std::string, Value> cols;
};

// Token kinds for the SQL lexer.
enum class TokenType {
    SELECT, FROM, WHERE, ORDER, BY, DESC, ASC, LIMIT, AND, OR,
    IDENTIFIER, NUMBER, STRING, LPAREN, RPAREN, COMMA,
    PLUS, MINUS, STAR, SLASH, CARET,
    EQ, NE, LT, GT, LE, GE,
    LOGICAL_AND, LOGICAL_OR,
    END, UNKNOWN
};

// Small token object used by the lexer and parser.
struct Token {
    TokenType type;
    std::string text;
    int line = 1;
    int col = 1;

    Token() : type(TokenType::UNKNOWN), text("") {}
    Token(TokenType t, const std::string& txt) : type(t), text(txt) {}
};
