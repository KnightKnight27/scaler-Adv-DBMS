#include "ShuntingYard.h"
#include <stdexcept>
#include <cctype>
#include <cmath>
#include <stack>
#include <vector>
#include <string>
#include <variant>

int ShuntingYard::precedence(char op) {
    if (op == '^') return 4;
    if (op == '*' || op == '/') return 3;
    if (op == '+' || op == '-') return 2;
    return 0;
}

bool ShuntingYard::is_right_associative(char op) {
    return op == '^';
}

double ShuntingYard::apply_op(double a, double b, char op) {
    switch (op) {
        case '+': return a + b;
        case '-': return a - b;
        case '*': return a * b;
        case '/':
            if (b == 0) throw std::invalid_argument("division by zero");
            return a / b;
        case '^': return std::pow(a, b);
        default:  throw std::invalid_argument("unknown operator");
    }
}

// Token types: number or operator/paren char
struct NumToken  { double value; };
struct CharToken { char   ch;    };
using Token = std::variant<NumToken, CharToken>;

double ShuntingYard::evaluate(const std::string& expr) {
    // Tokenize
    std::vector<Token> tokens;
    size_t i = 0;
    while (i < expr.size()) {
        if (std::isspace((unsigned char)expr[i])) { ++i; continue; }

        if (std::isdigit((unsigned char)expr[i]) || expr[i] == '.') {
            size_t j = i;
            while (j < expr.size() && (std::isdigit((unsigned char)expr[j]) || expr[j] == '.'))
                ++j;
            double val = std::stod(expr.substr(i, j - i));
            tokens.push_back(NumToken{val});
            i = j;
            continue;
        }

        char c = expr[i++];
        if (c == '+' || c == '-' || c == '*' || c == '/' || c == '^' || c == '(' || c == ')')
            tokens.push_back(CharToken{c});
        else
            throw std::invalid_argument(std::string("unexpected character: ") + c);
    }

    // Shunting-Yard: convert to postfix, detecting unary minus
    std::vector<Token> output;
    std::stack<char> ops;

    bool expect_operand = true;  // true at start or after '(' or after an operator

    for (size_t t = 0; t < tokens.size(); ++t) {
        auto& tok = tokens[t];

        if (std::holds_alternative<NumToken>(tok)) {
            output.push_back(tok);
            expect_operand = false;
            continue;
        }

        char c = std::get<CharToken>(tok).ch;

        if (c == '(') {
            ops.push(c);
            expect_operand = true;
            continue;
        }

        if (c == ')') {
            while (!ops.empty() && ops.top() != '(') {
                output.push_back(CharToken{ops.top()});
                ops.pop();
            }
            if (ops.empty()) throw std::invalid_argument("mismatched parentheses");
            ops.pop();  // discard '('
            expect_operand = false;
            continue;
        }

        // Operator
        if (expect_operand && c == '-') {
            // Unary minus: push 0 and treat as binary subtraction
            output.push_back(NumToken{0.0});
        }

        while (!ops.empty() && ops.top() != '(' &&
               (precedence(ops.top()) > precedence(c) ||
                (precedence(ops.top()) == precedence(c) && !is_right_associative(c)))) {
            output.push_back(CharToken{ops.top()});
            ops.pop();
        }
        ops.push(c);
        expect_operand = true;
    }

    while (!ops.empty()) {
        if (ops.top() == '(') throw std::invalid_argument("mismatched parentheses");
        output.push_back(CharToken{ops.top()});
        ops.pop();
    }

    // Evaluate postfix
    std::stack<double> eval;
    for (auto& tok : output) {
        if (std::holds_alternative<NumToken>(tok)) {
            eval.push(std::get<NumToken>(tok).value);
            continue;
        }
        char op = std::get<CharToken>(tok).ch;
        if (eval.size() < 2) throw std::invalid_argument("malformed expression");
        double b = eval.top(); eval.pop();
        double a = eval.top(); eval.pop();
        eval.push(apply_op(a, b, op));
    }

    if (eval.size() != 1) throw std::invalid_argument("malformed expression");
    return eval.top();
}
