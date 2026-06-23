#include "shunting_yard.h"
#include <iostream>
#include <stack>
#include <cctype>
#include <sstream>
#include <algorithm>
#include <stdexcept>

// Helper to print token information
std::string Token::toString() const {
    std::string typeStr;
    switch (type) {
        case TokenType::IDENTIFIER: typeStr = "IDENTIFIER"; break;
        case TokenType::LITERAL_NUMBER: typeStr = "LITERAL_NUMBER"; break;
        case TokenType::LITERAL_STRING: typeStr = "LITERAL_STRING"; break;
        case TokenType::OPERATOR: typeStr = "OPERATOR"; break;
        case TokenType::PARENTHESIS: typeStr = "PARENTHESIS"; break;
    }
    return typeStr + "(" + value + ")";
}

// Tokenize an infix expression string
std::vector<Token> tokenizeExpression(const std::string& expr) {
    std::vector<Token> tokens;
    size_t i = 0;
    size_t len = expr.length();
    while (i < len) {
        if (std::isspace(expr[i])) {
            i++;
            continue;
        }

        // Parentheses
        if (expr[i] == '(' || expr[i] == ')') {
            tokens.push_back({TokenType::PARENTHESIS, std::string(1, expr[i])});
            i++;
            continue;
        }

        // String Literals ('...')
        if (expr[i] == '\'') {
            i++; // Skip open quote
            std::string val;
            while (i < len && expr[i] != '\'') {
                val += expr[i];
                i++;
            }
            if (i < len && expr[i] == '\'') {
                i++; // Skip close quote
            }
            tokens.push_back({TokenType::LITERAL_STRING, val});
            continue;
        }

        // Multi-character operators: >=, <=, !=, <>
        if (i + 1 < len) {
            std::string op2 = expr.substr(i, 2);
            if (op2 == ">=" || op2 == "<=" || op2 == "!=" || op2 == "<>") {
                tokens.push_back({TokenType::OPERATOR, op2});
                i += 2;
                continue;
            }
        }

        // Single-character operators
        char op1 = expr[i];
        if (op1 == '>' || op1 == '<' || op1 == '=' || op1 == '+' || op1 == '-' || op1 == '*' || op1 == '/') {
            tokens.push_back({TokenType::OPERATOR, std::string(1, op1)});
            i++;
            continue;
        }

        // Numeric Literals
        if (std::isdigit(expr[i]) || (expr[i] == '.' && i + 1 < len && std::isdigit(expr[i+1]))) {
            std::string val;
            bool hasDot = false;
            while (i < len && (std::isdigit(expr[i]) || expr[i] == '.')) {
                if (expr[i] == '.') {
                    if (hasDot) break;
                    hasDot = true;
                }
                val += expr[i];
                i++;
            }
            tokens.push_back({TokenType::LITERAL_NUMBER, val});
            continue;
        }

        // Identifiers or Logical Operators (AND, OR, NOT)
        if (std::isalpha(expr[i]) || expr[i] == '_') {
            std::string val;
            while (i < len && (std::isalnum(expr[i]) || expr[i] == '_')) {
                val += expr[i];
                i++;
            }
            
            // Check for logical keywords
            std::string upperVal = val;
            std::transform(upperVal.begin(), upperVal.end(), upperVal.begin(), ::toupper);
            
            if (upperVal == "AND" || upperVal == "OR" || upperVal == "NOT") {
                tokens.push_back({TokenType::OPERATOR, upperVal});
            } else {
                tokens.push_back({TokenType::IDENTIFIER, val});
            }
            continue;
        }

        // Fallback / Unknown symbol (treat as operator so Parser flags it if invalid)
        tokens.push_back({TokenType::OPERATOR, std::string(1, expr[i])});
        i++;
    }
    return tokens;
}

// Operator Precedence
int getPrecedence(const std::string& op) {
    if (op == "*" || op == "/") return 5;
    if (op == "+" || op == "-") return 4;
    if (op == ">" || op == "<" || op == ">=" || op == "<=" || op == "=" || op == "!=" || op == "<>") return 3;
    if (op == "NOT") return 2;
    if (op == "AND") return 1;
    if (op == "OR") return 0;
    return -2;
}

// Convert infix tokens to postfix (RPN) using Dijkstra's Shunting-Yard
std::vector<Token> infixToPostfix(const std::vector<Token>& infix) {
    std::vector<Token> output;
    std::vector<Token> opStack;

    for (const auto& token : infix) {
        if (token.type == TokenType::LITERAL_NUMBER || 
            token.type == TokenType::LITERAL_STRING || 
            token.type == TokenType::IDENTIFIER) {
            output.push_back(token);
        } else if (token.type == TokenType::PARENTHESIS) {
            if (token.value == "(") {
                opStack.push_back(token);
            } else { // ")"
                while (!opStack.empty() && opStack.back().value != "(") {
                    output.push_back(opStack.back());
                    opStack.pop_back();
                }
                if (!opStack.empty()) {
                    opStack.pop_back(); // Remove "("
                }
            }
        } else if (token.type == TokenType::OPERATOR) {
            std::string op = token.value;
            int prec = getPrecedence(op);
            while (!opStack.empty() && opStack.back().type == TokenType::OPERATOR) {
                std::string topOp = opStack.back().value;
                int topPrec = getPrecedence(topOp);
                
                // NOT is right-associative (unary), others are left-associative
                if (op == "NOT") {
                    if (topPrec > prec) {
                        output.push_back(opStack.back());
                        opStack.pop_back();
                    } else {
                        break;
                    }
                } else {
                    if (topPrec >= prec) {
                        output.push_back(opStack.back());
                        opStack.pop_back();
                    } else {
                        break;
                    }
                }
            }
            opStack.push_back(token);
        }
    }

    while (!opStack.empty()) {
        output.push_back(opStack.back());
        opStack.pop_back();
    }

    return output;
}

// Intermediate structure for Stack-based Evaluation
struct EvalVal {
    enum class Type { NUMBER, STRING, BOOL };
    Type type;
    double numVal = 0.0;
    std::string strVal = "";
    bool boolVal = false;

    EvalVal(double n) : type(Type::NUMBER), numVal(n) {}
    EvalVal(const std::string& s) : type(Type::STRING), strVal(s) {}
    EvalVal(bool b) : type(Type::BOOL), boolVal(b) {}
};

// Parse a string to double safely
double parseDouble(const std::string& str) {
    try {
        return std::stod(str);
    } catch (...) {
        return 0.0;
    }
}

// Check if a string represents a number
bool isNumeric(const std::string& str) {
    if (str.empty()) return false;
    size_t offset = 0;
    if (str[0] == '-' || str[0] == '+') offset = 1;
    bool hasDot = false;
    for (size_t i = offset; i < str.length(); ++i) {
        if (str[i] == '.') {
            if (hasDot) return false;
            hasDot = true;
        } else if (!std::isdigit(str[i])) {
            return false;
        }
    }
    return true;
}

// Evaluate postfix RPN against a row
bool evaluateRPN(const std::vector<Token>& rpn, const std::unordered_map<std::string, std::string>& row) {
    std::vector<EvalVal> stack;

    for (const auto& token : rpn) {
        if (token.type == TokenType::LITERAL_NUMBER) {
            stack.push_back(EvalVal(parseDouble(token.value)));
        } else if (token.type == TokenType::LITERAL_STRING) {
            stack.push_back(EvalVal(token.value));
        } else if (token.type == TokenType::IDENTIFIER) {
            // Find in row
            auto it = row.find(token.value);
            if (it != row.end()) {
                std::string rawVal = it->second;
                // If it looks like a number, treat as number; otherwise string
                if (isNumeric(rawVal)) {
                    stack.push_back(EvalVal(parseDouble(rawVal)));
                } else {
                    stack.push_back(EvalVal(rawVal));
                }
            } else {
                stack.push_back(EvalVal("")); // Missing column acts as empty string
            }
        } else if (token.type == TokenType::OPERATOR) {
            if (token.value == "NOT") {
                if (stack.empty()) throw std::runtime_error("Empty stack for NOT operator");
                EvalVal val = stack.back();
                stack.pop_back();
                if (val.type == EvalVal::Type::BOOL) {
                    stack.push_back(EvalVal(!val.boolVal));
                } else if (val.type == EvalVal::Type::NUMBER) {
                    stack.push_back(EvalVal(val.numVal == 0.0));
                } else {
                    stack.push_back(EvalVal(val.strVal.empty()));
                }
                continue;
            }

            // Binary Operators
            if (stack.size() < 2) {
                throw std::runtime_error("Insufficient operands for operator: " + token.value);
            }
            EvalVal right = stack.back();
            stack.pop_back();
            EvalVal left = stack.back();
            stack.pop_back();

            std::string op = token.value;

            // Logical Operations
            if (op == "AND" || op == "OR") {
                bool lBool = (left.type == EvalVal::Type::BOOL) ? left.boolVal : (left.type == EvalVal::Type::NUMBER ? left.numVal != 0.0 : !left.strVal.empty());
                bool rBool = (right.type == EvalVal::Type::BOOL) ? right.boolVal : (right.type == EvalVal::Type::NUMBER ? right.numVal != 0.0 : !right.strVal.empty());
                
                if (op == "AND") {
                    stack.push_back(EvalVal(lBool && rBool));
                } else {
                    stack.push_back(EvalVal(lBool || rBool));
                }
                continue;
            }

            // Arithmetic Operations
            if (op == "+" || op == "-" || op == "*" || op == "/") {
                double lNum = (left.type == EvalVal::Type::NUMBER) ? left.numVal : parseDouble(left.strVal);
                double rNum = (right.type == EvalVal::Type::NUMBER) ? right.numVal : parseDouble(right.strVal);
                
                if (op == "+") stack.push_back(EvalVal(lNum + rNum));
                else if (op == "-") stack.push_back(EvalVal(lNum - rNum));
                else if (op == "*") stack.push_back(EvalVal(lNum * rNum));
                else if (op == "/") {
                    if (rNum == 0.0) throw std::runtime_error("Division by zero");
                    stack.push_back(EvalVal(lNum / rNum));
                }
                continue;
            }

            // Comparison Operations (>, <, >=, <=, =, !=, <>)
            bool isLeftNum = (left.type == EvalVal::Type::NUMBER);
            bool isRightNum = (right.type == EvalVal::Type::NUMBER);

            if (isLeftNum || isRightNum) {
                // Number comparison (convert the other side if it is a string representation)
                double lNum = isLeftNum ? left.numVal : parseDouble(left.strVal);
                double rNum = isRightNum ? right.numVal : parseDouble(right.strVal);

                if (op == "=") stack.push_back(EvalVal(lNum == rNum));
                else if (op == "!=" || op == "<>") stack.push_back(EvalVal(lNum != rNum));
                else if (op == ">") stack.push_back(EvalVal(lNum > rNum));
                else if (op == "<") stack.push_back(EvalVal(lNum < rNum));
                else if (op == ">=") stack.push_back(EvalVal(lNum >= rNum));
                else if (op == "<=") stack.push_back(EvalVal(lNum <= rNum));
            } else {
                // String comparison
                std::string lStr = left.strVal;
                std::string rStr = right.strVal;

                if (op == "=") stack.push_back(EvalVal(lStr == rStr));
                else if (op == "!=" || op == "<>") stack.push_back(EvalVal(lStr != rStr));
                else if (op == ">") stack.push_back(EvalVal(lStr > rStr));
                else if (op == "<") stack.push_back(EvalVal(lStr < rStr));
                else if (op == ">=") stack.push_back(EvalVal(lStr >= rStr));
                else if (op == "<=") stack.push_back(EvalVal(lStr <= rStr));
            }
        }
    }

    if (stack.size() != 1) {
        throw std::runtime_error("Invalid expression evaluation stack size");
    }

    EvalVal finalVal = stack.back();
    if (finalVal.type == EvalVal::Type::BOOL) return finalVal.boolVal;
    if (finalVal.type == EvalVal::Type::NUMBER) return finalVal.numVal != 0.0;
    return !finalVal.strVal.empty();
}
