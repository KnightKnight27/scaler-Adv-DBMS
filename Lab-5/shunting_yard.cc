// Lab 5 (Part 1) - Shunting-Yard expression evaluator (implementation)

#include "shunting_yard.h"

#include <cctype>
#include <cmath>
#include <stack>
#include <stdexcept>

namespace lab5 {

namespace {

// Operator -> (precedence, is-right-associative). Higher precedence binds
// tighter. Comparison/logical operators have lower precedence than
// arithmetic so "a + b > c" parses as "(a + b) > c".
struct OpInfo {
    int  precedence;
    bool right_assoc;
};

const std::unordered_map<std::string, OpInfo>& ops() {
    static const std::unordered_map<std::string, OpInfo> table = {
        {"||", {1, false}},   // logical OR
        {"&&", {2, false}},   // logical AND
        {"=",  {3, false}},   // equality
        {"!=", {3, false}},
        {"<",  {4, false}},
        {">",  {4, false}},
        {"<=", {4, false}},
        {">=", {4, false}},
        {"+",  {5, false}},
        {"-",  {5, false}},
        {"*",  {6, false}},
        {"/",  {6, false}},
    };
    return table;
}

bool is_operator(const std::string& tok) { return ops().count(tok) != 0; }

}  // namespace

std::vector<std::string> tokenize(const std::string& expr) {
    std::vector<std::string> tokens;
    int n = static_cast<int>(expr.size());
    int i = 0;
    while (i < n) {
        char c = expr[i];

        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
            continue;
        }

        // Number (allow a decimal point).
        if (std::isdigit(static_cast<unsigned char>(c)) ||
            (c == '.' && i + 1 < n &&
             std::isdigit(static_cast<unsigned char>(expr[i + 1])))) {
            int j = i;
            while (j < n && (std::isdigit(static_cast<unsigned char>(expr[j])) ||
                             expr[j] == '.')) {
                ++j;
            }
            tokens.push_back(expr.substr(i, j - i));
            i = j;
            continue;
        }

        // Identifier (column name): letters, digits, underscore.
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            int j = i;
            while (j < n && (std::isalnum(static_cast<unsigned char>(expr[j])) ||
                             expr[j] == '_')) {
                ++j;
            }
            tokens.push_back(expr.substr(i, j - i));
            i = j;
            continue;
        }

        // Parentheses.
        if (c == '(' || c == ')') {
            tokens.push_back(std::string(1, c));
            ++i;
            continue;
        }

        // Two-character operators first, then single-character.
        if (i + 1 < n) {
            std::string two = expr.substr(i, 2);
            if (is_operator(two)) {
                tokens.push_back(two);
                i += 2;
                continue;
            }
        }
        std::string one(1, c);
        if (is_operator(one)) {
            tokens.push_back(one);
            ++i;
            continue;
        }

        throw std::runtime_error("tokenize: unexpected character '" +
                                 std::string(1, c) + "'");
    }
    return tokens;
}

std::vector<std::string> to_rpn(const std::vector<std::string>& tokens) {
    std::vector<std::string> output;
    std::stack<std::string>  op_stack;

    for (const auto& tok : tokens) {
        if (tok == "(") {
            op_stack.push(tok);
        } else if (tok == ")") {
            while (!op_stack.empty() && op_stack.top() != "(") {
                output.push_back(op_stack.top());
                op_stack.pop();
            }
            if (op_stack.empty()) {
                throw std::runtime_error("to_rpn: mismatched parentheses");
            }
            op_stack.pop();  // discard the '('
        } else if (is_operator(tok)) {
            const OpInfo& cur = ops().at(tok);
            while (!op_stack.empty() && is_operator(op_stack.top())) {
                const OpInfo& top = ops().at(op_stack.top());
                bool pop_top = top.precedence > cur.precedence ||
                               (top.precedence == cur.precedence &&
                                !cur.right_assoc);
                if (!pop_top) break;
                output.push_back(op_stack.top());
                op_stack.pop();
            }
            op_stack.push(tok);
        } else {
            // number or identifier
            output.push_back(tok);
        }
    }

    while (!op_stack.empty()) {
        if (op_stack.top() == "(") {
            throw std::runtime_error("to_rpn: mismatched parentheses");
        }
        output.push_back(op_stack.top());
        op_stack.pop();
    }
    return output;
}

double eval_rpn(const std::vector<std::string>& rpn,
                const std::unordered_map<std::string, double>& vars) {
    std::stack<double> stk;

    for (const auto& tok : rpn) {
        if (is_operator(tok)) {
            if (stk.size() < 2) {
                throw std::runtime_error("eval_rpn: not enough operands for '" +
                                         tok + "'");
            }
            double b = stk.top(); stk.pop();
            double a = stk.top(); stk.pop();
            double r = 0.0;
            if      (tok == "+")  r = a + b;
            else if (tok == "-")  r = a - b;
            else if (tok == "*")  r = a * b;
            else if (tok == "/")  r = a / b;
            else if (tok == "<")  r = (a <  b) ? 1.0 : 0.0;
            else if (tok == ">")  r = (a >  b) ? 1.0 : 0.0;
            else if (tok == "<=") r = (a <= b) ? 1.0 : 0.0;
            else if (tok == ">=") r = (a >= b) ? 1.0 : 0.0;
            else if (tok == "=")  r = (a == b) ? 1.0 : 0.0;
            else if (tok == "!=") r = (a != b) ? 1.0 : 0.0;
            else if (tok == "&&") r = (a != 0.0 && b != 0.0) ? 1.0 : 0.0;
            else if (tok == "||") r = (a != 0.0 || b != 0.0) ? 1.0 : 0.0;
            stk.push(r);
        } else {
            // Try to read it as a number; otherwise treat as a variable.
            try {
                std::size_t pos = 0;
                double num = std::stod(tok, &pos);
                if (pos == tok.size()) {
                    stk.push(num);
                    continue;
                }
            } catch (...) {
                // fall through to variable lookup
            }
            auto it = vars.find(tok);
            if (it == vars.end()) {
                throw std::runtime_error("eval_rpn: unknown variable '" + tok + "'");
            }
            stk.push(it->second);
        }
    }

    if (stk.size() != 1) {
        throw std::runtime_error("eval_rpn: malformed expression");
    }
    return stk.top();
}

}  // namespace lab5
