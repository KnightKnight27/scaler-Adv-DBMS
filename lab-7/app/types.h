#pragma once

#include <string>
#include <variant>
#include <unordered_map>

// Basic value type supporting both numeric and string values
using Value = std::variant<double, std::string>;

// Represents a single row in the result set
struct Row {
    std::unordered_map<std::string, Value> cols;
};

// Token types for SQL lexing
enum class TokenType {
    SELECT, FROM, WHERE, ORDER, BY, DESC, ASC, LIMIT, AND, OR,
    IDENTIFIER, NUMBER, STRING, LPAREN, RPAREN, COMMA,
    PLUS, MINUS, STAR, SLASH, CARET,
    EQ, NE, LT, GT, LE, GE,
    LOGICAL_AND, LOGICAL_OR,
    END, UNKNOWN
};

// Token representation
struct Token {
    TokenType type;
    std::string text;
    int line = 1;
    int col = 1;

    Token() : type(TokenType::UNKNOWN), text("") {}
    Token(TokenType t, const std::string& txt) : type(t), text(txt) {}
};
