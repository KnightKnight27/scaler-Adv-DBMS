#pragma once

#include <string>
#include <variant>
#include <unordered_map>
#include <iostream>

enum class TokenType {
    SELECT, FROM, WHERE, AND, OR, LIMIT, ORDER, BY, DESC, ASC,
    IDENTIFIER, NUMBER, STRING,
    GT, LT, GE, LE, EQ, NE,
    LPAREN, RPAREN, COMMA, STAR, END
};

struct Token {
    TokenType type;
    std::string text;
};

using Value = std::variant<int, double, std::string>;
using Row = std::unordered_map<std::string, Value>;

inline void printValue(const Value& val) {
    if (std::holds_alternative<int>(val)) {
        std::cout << std::get<int>(val);
    } else if (std::holds_alternative<double>(val)) {
        std::cout << std::get<double>(val);
    } else if (std::holds_alternative<std::string>(val)) {
        std::cout << std::get<std::string>(val);
    }
}
