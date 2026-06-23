#include "ShuntingYard.h"
#include <stack>
#include <cctype>
#include <stdexcept>
#include <vector>
#include <iostream>

// Helper to determine operator precedence
static int getPrecedence(const std::string& op) {
    if (op == "OR") return 1;
    if (op == "AND") return 2;
    if (op == "=" || op == ">" || op == "<" || op == ">=" || op == "<=") return 3;
    if (op == "+" || op == "-") return 4;
    if (op == "*" || op == "/") return 5;
    return 0;
}

// Tokenizer
static std::vector<std::string> tokenize(const std::string& expr) {
    std::vector<std::string> tokens;
    std::string current;
    bool in_string = false;
    for (size_t i = 0; i < expr.length(); ++i) {
        char c = expr[i];
        if (in_string) {
            current += c;
            if (c == '\'') {
                in_string = false;
                tokens.push_back(current);
                current.clear();
            }
        } else {
            if (c == '\'') {
                in_string = true;
                current += c;
            } else if (std::isspace(c)) {
                if (!current.empty()) { tokens.push_back(current); current.clear(); }
            } else if (c == '(' || c == ')') {
                if (!current.empty()) { tokens.push_back(current); current.clear(); }
                tokens.push_back(std::string(1, c));
            } else if (c == '=' || c == '<' || c == '>') {
                if (!current.empty()) { tokens.push_back(current); current.clear(); }
                current += c;
                if (i + 1 < expr.length() && expr[i + 1] == '=') {
                    current += expr[++i];
                }
                tokens.push_back(current);
                current.clear();
            } else if (c == '+' || c == '-' || c == '*' || c == '/') {
                if (!current.empty()) { tokens.push_back(current); current.clear(); }
                tokens.push_back(std::string(1, c));
            } else {
                current += c;
            }
        }
    }
    if (!current.empty()) tokens.push_back(current);
    return tokens;
}

// Value resolution
static Value resolveValue(const std::string& token, const Row& row) {
    if (token == "TRUE") return true;
    if (token == "FALSE") return false;
    if (token.front() == '\'' && token.back() == '\'') return token.substr(1, token.length() - 2);
    if (std::isdigit(token.front()) || (token.front() == '-' && token.length() > 1 && std::isdigit(token[1]))) {
        return std::stoi(token);
    }
    auto it = row.find(token);
    if (it != row.end()) return it->second;
    throw std::runtime_error("Unknown column or invalid literal: " + token);
}

// Operator application
static Value applyOp(const std::string& op, const Value& lhs, const Value& rhs) {
    if (op == "AND") return std::get<bool>(lhs) && std::get<bool>(rhs);
    if (op == "OR") return std::get<bool>(lhs) || std::get<bool>(rhs);
    
    if (std::holds_alternative<int>(lhs) && std::holds_alternative<int>(rhs)) {
        int l = std::get<int>(lhs), r = std::get<int>(rhs);
        if (op == "=") return l == r;
        if (op == ">") return l > r;
        if (op == "<") return l < r;
        if (op == ">=") return l >= r;
        if (op == "<=") return l <= r;
        if (op == "+") return l + r;
        if (op == "-") return l - r;
        if (op == "*") return l * r;
        if (op == "/") return l / r;
    } else if (std::holds_alternative<std::string>(lhs) && std::holds_alternative<std::string>(rhs)) {
        std::string l = std::get<std::string>(lhs), r = std::get<std::string>(rhs);
        if (op == "=") return l == r;
    }
    throw std::runtime_error("Unsupported operation or type mismatch for operator: " + op);
}

bool ShuntingYard::evaluate(const std::string& expression, const Row& row) {
    std::vector<std::string> tokens = tokenize(expression);
    std::vector<std::string> postfix;
    std::stack<std::string> ops;
    
    for (const auto& token : tokens) {
        if (token == "AND" || token == "OR" || token == "=" || token == ">" || token == "<" || token == ">=" || token == "<=" || token == "+" || token == "-" || token == "*" || token == "/") {
            while (!ops.empty() && ops.top() != "(" && getPrecedence(ops.top()) >= getPrecedence(token)) {
                postfix.push_back(ops.top());
                ops.pop();
            }
            ops.push(token);
        } else if (token == "(") {
            ops.push(token);
        } else if (token == ")") {
            while (!ops.empty() && ops.top() != "(") {
                postfix.push_back(ops.top());
                ops.pop();
            }
            if (!ops.empty()) ops.pop(); // Pop "("
        } else {
            postfix.push_back(token); // Operand
        }
    }
    while (!ops.empty()) {
        postfix.push_back(ops.top());
        ops.pop();
    }
    
    std::stack<Value> evalStack;
    for (const auto& token : postfix) {
        if (token == "AND" || token == "OR" || token == "=" || token == ">" || token == "<" || token == ">=" || token == "<=" || token == "+" || token == "-" || token == "*" || token == "/") {
            if (evalStack.size() < 2) throw std::runtime_error("Invalid expression");
            Value rhs = evalStack.top(); evalStack.pop();
            Value lhs = evalStack.top(); evalStack.pop();
            evalStack.push(applyOp(token, lhs, rhs));
        } else {
            evalStack.push(resolveValue(token, row));
        }
    }
    
    if (evalStack.size() != 1) throw std::runtime_error("Invalid expression evaluation");
    Value finalResult = evalStack.top();
    if (!std::holds_alternative<bool>(finalResult)) {
        throw std::runtime_error("Expression did not evaluate to a boolean");
    }
    return std::get<bool>(finalResult);
}
