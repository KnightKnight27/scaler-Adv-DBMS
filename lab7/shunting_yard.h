#pragma once
#include <string>
#include <vector>
#include <unordered_map>

enum class TokenType {
    IDENTIFIER,
    LITERAL_NUMBER,
    LITERAL_STRING,
    OPERATOR,
    PARENTHESIS
};

struct Token {
    TokenType type;
    std::string value;

    // Helper to print token information
    std::string toString() const;
};

// Tokenize an infix expression string
std::vector<Token> tokenizeExpression(const std::string& expr);

// Convert infix tokens to postfix (RPN) using Dijkstra's Shunting-Yard
std::vector<Token> infixToPostfix(const std::vector<Token>& infix);

// Evaluate the RPN tokens against a specific database Row
bool evaluateRPN(const std::vector<Token>& rpn, const std::unordered_map<std::string, std::string>& row);
