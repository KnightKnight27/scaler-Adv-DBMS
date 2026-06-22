// Lab 7 (Part 1) - Dijkstra's Shunting Yard Algorithm
// Author: 24BCS10345 Ansh Mahajan
//
// Converts an infix arithmetic expression to postfix (Reverse Polish Notation)
// using two stacks-worth of bookkeeping, then evaluates the postfix form.
//
// Supported:
//   - numbers (integer or decimal)
//   - binary operators + - * / with usual precedence, and ^ (right associative)
//   - parentheses
//   - unary minus, e.g. -5 or 4 * -2  (internally the 'm' operator)
//
// The whole point of the algorithm: operator precedence and associativity are
// resolved purely by comparing the incoming operator against the top of the
// operator stack - no recursion and no grammar tables.
//
// Build: g++ -std=c++17 shunting_yard.cpp -o shunting
// Run:   ./shunting

#include <cctype>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Token {
    enum class Kind { Number, Operator, LParen, RParen } kind;
    double value = 0.0;   // for Number
    char op = '\0';       // for Operator ('+','-','*','/','^','m' = unary minus)
};

bool isOperatorChar(char c) {
    return c == '+' || c == '-' || c == '*' || c == '/' || c == '^';
}

// Higher number binds tighter. Unary minus ('m') sits above the binary
// operators but below exponentiation so that, e.g., -2 * 3 = -6.
int precedence(char op) {
    switch (op) {
        case '+':
        case '-': return 1;
        case '*':
        case '/': return 2;
        case 'm': return 3;   // unary minus
        case '^': return 4;
        default:  return 0;
    }
}

bool isRightAssociative(char op) {
    return op == '^' || op == 'm';
}

// Split the raw text into tokens. A '-' or '+' is unary when it appears at the
// start, or right after another operator or an opening parenthesis.
std::vector<Token> tokenize(const std::string& expr) {
    std::vector<Token> tokens;
    bool expectOperand = true;   // true when the next token should be a value

    for (std::size_t i = 0; i < expr.size();) {
        char c = expr[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            std::size_t j = i;
            while (j < expr.size() &&
                   (std::isdigit(static_cast<unsigned char>(expr[j])) ||
                    expr[j] == '.')) {
                ++j;
            }
            Token t;
            t.kind = Token::Kind::Number;
            t.value = std::stod(expr.substr(i, j - i));
            tokens.push_back(t);
            expectOperand = false;
            i = j;
        } else if (c == '(') {
            tokens.push_back({Token::Kind::LParen, 0.0, '\0'});
            expectOperand = true;
            ++i;
        } else if (c == ')') {
            tokens.push_back({Token::Kind::RParen, 0.0, '\0'});
            expectOperand = false;
            ++i;
        } else if (isOperatorChar(c)) {
            char op = c;
            if (expectOperand) {
                if (c == '-') {
                    op = 'm';                 // unary minus
                } else if (c == '+') {
                    ++i;                      // unary plus: harmless, skip it
                    continue;
                } else {
                    throw std::runtime_error(std::string("unexpected operator '") +
                                             c + "'");
                }
            }
            Token t;
            t.kind = Token::Kind::Operator;
            t.op = op;
            tokens.push_back(t);
            expectOperand = true;
            ++i;
        } else {
            throw std::runtime_error(std::string("illegal character '") + c + "'");
        }
    }
    return tokens;
}

// Shunting Yard: read tokens left to right, output numbers immediately and use
// the operator stack to release operators in the right order.
std::vector<Token> toPostfix(const std::vector<Token>& tokens) {
    std::vector<Token> output;
    std::stack<Token> ops;

    for (const Token& t : tokens) {
        switch (t.kind) {
            case Token::Kind::Number:
                output.push_back(t);
                break;
            case Token::Kind::Operator:
                while (!ops.empty() && ops.top().kind == Token::Kind::Operator) {
                    char top = ops.top().op;
                    bool pop = (precedence(top) > precedence(t.op)) ||
                               (precedence(top) == precedence(t.op) &&
                                !isRightAssociative(t.op));
                    if (!pop) {
                        break;
                    }
                    output.push_back(ops.top());
                    ops.pop();
                }
                ops.push(t);
                break;
            case Token::Kind::LParen:
                ops.push(t);
                break;
            case Token::Kind::RParen:
                while (!ops.empty() && ops.top().kind != Token::Kind::LParen) {
                    output.push_back(ops.top());
                    ops.pop();
                }
                if (ops.empty()) {
                    throw std::runtime_error("mismatched parentheses");
                }
                ops.pop();   // discard the '('
                break;
        }
    }
    while (!ops.empty()) {
        if (ops.top().kind == Token::Kind::LParen) {
            throw std::runtime_error("mismatched parentheses");
        }
        output.push_back(ops.top());
        ops.pop();
    }
    return output;
}

double applyBinary(char op, double a, double b) {
    switch (op) {
        case '+': return a + b;
        case '-': return a - b;
        case '*': return a * b;
        case '/':
            if (b == 0.0) {
                throw std::runtime_error("division by zero");
            }
            return a / b;
        case '^': return std::pow(a, b);
        default:  throw std::runtime_error("unknown operator");
    }
}

double evaluatePostfix(const std::vector<Token>& postfix) {
    std::stack<double> values;
    for (const Token& t : postfix) {
        if (t.kind == Token::Kind::Number) {
            values.push(t.value);
        } else if (t.op == 'm') {            // unary minus
            if (values.empty()) {
                throw std::runtime_error("malformed expression");
            }
            double x = values.top();
            values.pop();
            values.push(-x);
        } else {
            if (values.size() < 2) {
                throw std::runtime_error("malformed expression");
            }
            double b = values.top(); values.pop();
            double a = values.top(); values.pop();
            values.push(applyBinary(t.op, a, b));
        }
    }
    if (values.size() != 1) {
        throw std::runtime_error("malformed expression");
    }
    return values.top();
}

std::string postfixToString(const std::vector<Token>& postfix) {
    std::ostringstream out;
    for (std::size_t i = 0; i < postfix.size(); ++i) {
        const Token& t = postfix[i];
        if (t.kind == Token::Kind::Number) {
            out << t.value;
        } else if (t.op == 'm') {
            out << "u-";                     // show unary minus distinctly
        } else {
            out << t.op;
        }
        if (i + 1 < postfix.size()) {
            out << ' ';
        }
    }
    return out.str();
}

void run(const std::string& expr) {
    std::cout << "infix:   " << expr << '\n';
    try {
        std::vector<Token> postfix = toPostfix(tokenize(expr));
        std::cout << "postfix: " << postfixToString(postfix) << '\n';
        std::cout << "result:  " << evaluatePostfix(postfix) << "\n\n";
    } catch (const std::exception& e) {
        std::cout << "error:   " << e.what() << "\n\n";
    }
}

}  // namespace

int main() {
    // Curated cases: precedence, left/right associativity, parentheses, unary
    // minus, and a deliberate error.
    const std::vector<std::string> samples = {
        "3 + 4 * 2",
        "(3 + 4) * 2",
        "10 - 2 - 3",        // left associative => 5
        "2 ^ 3 ^ 2",         // right associative => 512
        "4 * -2",            // unary minus after an operator
        "-(5 - 8)",          // => 3
        "100 / (2 + 3) / 2",
        "7 + (6 * 5 + 1",    // mismatched parenthesis -> error
    };

    std::cout << "=== Shunting Yard: infix -> postfix -> value ===\n\n";
    for (const std::string& s : samples) {
        run(s);
    }

    // Anything piped in on stdin is evaluated too (one expression per line).
    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty()) {
            run(line);
        }
    }
    return 0;
}
