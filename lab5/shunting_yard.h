#pragma once
/**
 * Lab 5 — Dijkstra's Shunting-Yard Algorithm
 *
 * Converts infix expressions to postfix (Reverse Polish Notation)
 * and evaluates them. Handles:
 *   - Arithmetic operators: +, -, *, /, %
 *   - Comparison operators: ==, !=, <, >, <=, >=
 *   - Logical operators: AND, OR, NOT
 *   - Parentheses for grouping
 *   - Numeric and string literals
 */

#include <iostream>
#include <string>
#include <vector>
#include <stack>
#include <queue>
#include <variant>
#include <sstream>
#include <stdexcept>
#include <cctype>
#include <cmath>
#include <algorithm>

// ─────────────────────────────────────────────────
// Token types for the expression evaluator
// ─────────────────────────────────────────────────
enum class TokenType {
    NUMBER,
    STRING,
    IDENTIFIER,
    PLUS, MINUS, MULTIPLY, DIVIDE, MODULO,
    EQ, NEQ, LT, GT, LTE, GTE,
    AND, OR, NOT,
    LPAREN, RPAREN,
    COMMA,
    END
};

struct Token {
    TokenType   type;
    std::string value;

    Token(TokenType t, const std::string& v = "") : type(t), value(v) {}

    bool is_operator() const {
        return type >= TokenType::PLUS && type <= TokenType::NOT;
    }

    bool is_binary_operator() const {
        return is_operator() && type != TokenType::NOT;
    }

    std::string to_string() const {
        switch (type) {
            case TokenType::NUMBER:     return value;
            case TokenType::STRING:     return "'" + value + "'";
            case TokenType::IDENTIFIER: return value;
            case TokenType::PLUS:       return "+";
            case TokenType::MINUS:      return "-";
            case TokenType::MULTIPLY:   return "*";
            case TokenType::DIVIDE:     return "/";
            case TokenType::MODULO:     return "%";
            case TokenType::EQ:         return "==";
            case TokenType::NEQ:        return "!=";
            case TokenType::LT:         return "<";
            case TokenType::GT:         return ">";
            case TokenType::LTE:        return "<=";
            case TokenType::GTE:        return ">=";
            case TokenType::AND:        return "AND";
            case TokenType::OR:         return "OR";
            case TokenType::NOT:        return "NOT";
            case TokenType::LPAREN:     return "(";
            case TokenType::RPAREN:     return ")";
            case TokenType::COMMA:      return ",";
            case TokenType::END:        return "END";
        }
        return "?";
    }
};

// ─────────────────────────────────────────────────
// Value type (can be number or string)
// ─────────────────────────────────────────────────
using Value = std::variant<double, std::string>;

inline std::string value_to_string(const Value& v) {
    if (std::holds_alternative<double>(v)) {
        double d = std::get<double>(v);
        if (d == static_cast<int>(d)) return std::to_string(static_cast<int>(d));
        return std::to_string(d);
    }
    return std::get<std::string>(v);
}

inline double value_to_double(const Value& v) {
    if (std::holds_alternative<double>(v)) return std::get<double>(v);
    try { return std::stod(std::get<std::string>(v)); }
    catch (...) { return 0.0; }
}

inline bool value_to_bool(const Value& v) {
    if (std::holds_alternative<double>(v)) return std::get<double>(v) != 0.0;
    return !std::get<std::string>(v).empty();
}

// ─────────────────────────────────────────────────
// Tokenizer
// ─────────────────────────────────────────────────
class ExprTokenizer {
public:
    static std::vector<Token> tokenize(const std::string& expr) {
        std::vector<Token> tokens;
        size_t i = 0;

        while (i < expr.size()) {
            // Skip whitespace
            if (std::isspace(expr[i])) { i++; continue; }

            // Numbers
            if (std::isdigit(expr[i]) || (expr[i] == '.' && i + 1 < expr.size() && std::isdigit(expr[i+1]))) {
                size_t start = i;
                while (i < expr.size() && (std::isdigit(expr[i]) || expr[i] == '.')) i++;
                tokens.emplace_back(TokenType::NUMBER, expr.substr(start, i - start));
                continue;
            }

            // String literals
            if (expr[i] == '\'' || expr[i] == '"') {
                char quote = expr[i++];
                size_t start = i;
                while (i < expr.size() && expr[i] != quote) i++;
                tokens.emplace_back(TokenType::STRING, expr.substr(start, i - start));
                if (i < expr.size()) i++;  // skip closing quote
                continue;
            }

            // Identifiers and keywords
            if (std::isalpha(expr[i]) || expr[i] == '_') {
                size_t start = i;
                while (i < expr.size() && (std::isalnum(expr[i]) || expr[i] == '_')) i++;
                std::string word = expr.substr(start, i - start);
                std::string upper = word;
                std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

                if (upper == "AND")       tokens.emplace_back(TokenType::AND, word);
                else if (upper == "OR")   tokens.emplace_back(TokenType::OR, word);
                else if (upper == "NOT")  tokens.emplace_back(TokenType::NOT, word);
                else                      tokens.emplace_back(TokenType::IDENTIFIER, word);
                continue;
            }

            // Two-character operators
            if (i + 1 < expr.size()) {
                std::string two = expr.substr(i, 2);
                if (two == "==") { tokens.emplace_back(TokenType::EQ); i += 2; continue; }
                if (two == "!=") { tokens.emplace_back(TokenType::NEQ); i += 2; continue; }
                if (two == "<=") { tokens.emplace_back(TokenType::LTE); i += 2; continue; }
                if (two == ">=") { tokens.emplace_back(TokenType::GTE); i += 2; continue; }
            }

            // Single-character operators
            switch (expr[i]) {
                case '+': tokens.emplace_back(TokenType::PLUS); break;
                case '-': tokens.emplace_back(TokenType::MINUS); break;
                case '*': tokens.emplace_back(TokenType::MULTIPLY); break;
                case '/': tokens.emplace_back(TokenType::DIVIDE); break;
                case '%': tokens.emplace_back(TokenType::MODULO); break;
                case '<': tokens.emplace_back(TokenType::LT); break;
                case '>': tokens.emplace_back(TokenType::GT); break;
                case '=': tokens.emplace_back(TokenType::EQ); break;
                case '(': tokens.emplace_back(TokenType::LPAREN); break;
                case ')': tokens.emplace_back(TokenType::RPAREN); break;
                case ',': tokens.emplace_back(TokenType::COMMA); break;
                default:
                    throw std::runtime_error("Unexpected character: " + std::string(1, expr[i]));
            }
            i++;
        }
        tokens.emplace_back(TokenType::END);
        return tokens;
    }
};

// ─────────────────────────────────────────────────
// Shunting-Yard Algorithm
// ─────────────────────────────────────────────────
class ShuntingYard {
private:
    // Operator precedence (higher = binds tighter)
    static int precedence(TokenType type) {
        switch (type) {
            case TokenType::OR:       return 1;
            case TokenType::AND:      return 2;
            case TokenType::NOT:      return 3;
            case TokenType::EQ:
            case TokenType::NEQ:      return 4;
            case TokenType::LT:
            case TokenType::GT:
            case TokenType::LTE:
            case TokenType::GTE:      return 5;
            case TokenType::PLUS:
            case TokenType::MINUS:    return 6;
            case TokenType::MULTIPLY:
            case TokenType::DIVIDE:
            case TokenType::MODULO:   return 7;
            default:                  return 0;
        }
    }

    // Left-associative (all binary operators in this implementation)
    static bool is_left_assoc(TokenType type) {
        return type != TokenType::NOT;  // NOT is right-associative
    }

public:
    /**
     * Convert infix tokens to postfix (RPN) using Dijkstra's Shunting-Yard
     *
     * Algorithm:
     * - For each token:
     *   - Number/String/Identifier → output queue
     *   - Operator → pop higher-precedence ops from stack to output, then push
     *   - Left paren → push to stack
     *   - Right paren → pop to output until left paren found
     */
    static std::vector<Token> to_postfix(const std::vector<Token>& tokens) {
        std::vector<Token> output;
        std::stack<Token> op_stack;

        for (const auto& token : tokens) {
            if (token.type == TokenType::END) break;

            if (token.type == TokenType::NUMBER ||
                token.type == TokenType::STRING ||
                token.type == TokenType::IDENTIFIER) {
                output.push_back(token);
            }
            else if (token.is_operator()) {
                while (!op_stack.empty() &&
                       op_stack.top().type != TokenType::LPAREN &&
                       op_stack.top().is_operator() &&
                       (precedence(op_stack.top().type) > precedence(token.type) ||
                        (precedence(op_stack.top().type) == precedence(token.type) &&
                         is_left_assoc(token.type)))) {
                    output.push_back(op_stack.top());
                    op_stack.pop();
                }
                op_stack.push(token);
            }
            else if (token.type == TokenType::LPAREN) {
                op_stack.push(token);
            }
            else if (token.type == TokenType::RPAREN) {
                while (!op_stack.empty() && op_stack.top().type != TokenType::LPAREN) {
                    output.push_back(op_stack.top());
                    op_stack.pop();
                }
                if (op_stack.empty()) {
                    throw std::runtime_error("Mismatched parentheses");
                }
                op_stack.pop();  // discard '('
            }
        }

        // Pop remaining operators
        while (!op_stack.empty()) {
            if (op_stack.top().type == TokenType::LPAREN) {
                throw std::runtime_error("Mismatched parentheses");
            }
            output.push_back(op_stack.top());
            op_stack.pop();
        }

        return output;
    }

    /**
     * Evaluate a postfix expression
     */
    static Value evaluate(const std::vector<Token>& postfix,
                          const std::unordered_map<std::string, Value>& vars = {}) {
        std::stack<Value> eval_stack;

        for (const auto& token : postfix) {
            if (token.type == TokenType::NUMBER) {
                eval_stack.push(std::stod(token.value));
            }
            else if (token.type == TokenType::STRING) {
                eval_stack.push(token.value);
            }
            else if (token.type == TokenType::IDENTIFIER) {
                auto it = vars.find(token.value);
                if (it != vars.end()) {
                    eval_stack.push(it->second);
                } else {
                    throw std::runtime_error("Undefined variable: " + token.value);
                }
            }
            else if (token.type == TokenType::NOT) {
                if (eval_stack.empty()) throw std::runtime_error("NOT: missing operand");
                Value a = eval_stack.top(); eval_stack.pop();
                eval_stack.push(value_to_bool(a) ? 0.0 : 1.0);
            }
            else if (token.is_binary_operator()) {
                if (eval_stack.size() < 2) throw std::runtime_error("Not enough operands");
                Value b = eval_stack.top(); eval_stack.pop();
                Value a = eval_stack.top(); eval_stack.pop();

                double da = value_to_double(a), db = value_to_double(b);
                std::string sa = value_to_string(a), sb = value_to_string(b);

                switch (token.type) {
                    case TokenType::PLUS:     eval_stack.push(da + db); break;
                    case TokenType::MINUS:    eval_stack.push(da - db); break;
                    case TokenType::MULTIPLY: eval_stack.push(da * db); break;
                    case TokenType::DIVIDE:
                        if (db == 0) throw std::runtime_error("Division by zero");
                        eval_stack.push(da / db);
                        break;
                    case TokenType::MODULO:
                        eval_stack.push(std::fmod(da, db));
                        break;
                    case TokenType::EQ:
                        if (std::holds_alternative<std::string>(a) || std::holds_alternative<std::string>(b))
                            eval_stack.push(sa == sb ? 1.0 : 0.0);
                        else
                            eval_stack.push(da == db ? 1.0 : 0.0);
                        break;
                    case TokenType::NEQ:
                        if (std::holds_alternative<std::string>(a) || std::holds_alternative<std::string>(b))
                            eval_stack.push(sa != sb ? 1.0 : 0.0);
                        else
                            eval_stack.push(da != db ? 1.0 : 0.0);
                        break;
                    case TokenType::LT:   eval_stack.push(da < db ? 1.0 : 0.0); break;
                    case TokenType::GT:   eval_stack.push(da > db ? 1.0 : 0.0); break;
                    case TokenType::LTE:  eval_stack.push(da <= db ? 1.0 : 0.0); break;
                    case TokenType::GTE:  eval_stack.push(da >= db ? 1.0 : 0.0); break;
                    case TokenType::AND:
                        eval_stack.push((value_to_bool(a) && value_to_bool(b)) ? 1.0 : 0.0);
                        break;
                    case TokenType::OR:
                        eval_stack.push((value_to_bool(a) || value_to_bool(b)) ? 1.0 : 0.0);
                        break;
                    default: break;
                }
            }
        }

        if (eval_stack.empty()) throw std::runtime_error("Empty expression");
        return eval_stack.top();
    }

    /**
     * Convenience: tokenize + convert + evaluate in one call
     */
    static Value eval(const std::string& expr,
                      const std::unordered_map<std::string, Value>& vars = {}) {
        auto tokens = ExprTokenizer::tokenize(expr);
        auto postfix = to_postfix(tokens);
        return evaluate(postfix, vars);
    }
};
