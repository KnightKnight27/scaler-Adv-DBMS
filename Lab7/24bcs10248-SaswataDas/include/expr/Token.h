#pragma once

#include <string>

namespace expr {

// A lexical unit produced by the Tokenizer and consumed by the
// infix-to-postfix converter and the RPN evaluator.
enum class TokenType {
    Number,
    Operator,
    LeftParen,
    RightParen
};

struct Token {
    TokenType type;
    double number = 0.0;  // valid only when type == Number
    char op = '\0';       // valid only when type == Operator

    static Token makeNumber(double value) { return {TokenType::Number, value, '\0'}; }
    static Token makeOperator(char symbol) { return {TokenType::Operator, 0.0, symbol}; }
    static Token makeLeftParen() { return {TokenType::LeftParen, 0.0, '\0'}; }
    static Token makeRightParen() { return {TokenType::RightParen, 0.0, '\0'}; }
};

// Human-readable form, used by the demo to print the RPN stream.
std::string toString(const Token& token);

}  // namespace expr
