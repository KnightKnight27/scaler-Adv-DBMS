// Lab 7 - Shunting-Yard evaluator for a SQL WHERE clause
// Bibek Jyoti Charah (24bcs10112)
//
// Lex the infix condition, convert it to RPN with the shunting-yard
// algorithm, then evaluate the RPN once per row with an integer stack.
// Supports: id/age columns, integer literals, comparisons (> < >= <= = !=),
// AND/OR (also && / ||), and parentheses.

#include <cctype>
#include <iostream>
#include <stack>
#include <stdexcept>
#include <string>
#include <vector>

struct Row {
    std::string name;
    int id;
    int age;
};

namespace {

int precedence(const std::string &op) {
    if (op == "OR") return 1;
    if (op == "AND") return 2;
    if (op == "=" || op == "!=") return 3;
    if (op == "<" || op == ">" || op == "<=" || op == ">=") return 4;
    return 0;
}

bool isOperator(const std::string &s) { return precedence(s) > 0; }

bool isNumber(const std::string &s) {
    for (char c : s)
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    return !s.empty();
}

int column(const std::string &name, const Row &r) {
    if (name == "id") return r.id;
    if (name == "age") return r.age;
    throw std::runtime_error("unknown column: " + name);
}

}  // namespace

std::vector<std::string> lex(const std::string &q) {
    std::vector<std::string> tokens;
    for (std::size_t i = 0; i < q.size();) {
        char c = q[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
        } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::size_t j = i;
            while (j < q.size() && (std::isalnum(static_cast<unsigned char>(q[j])) || q[j] == '_')) ++j;
            std::string word = q.substr(i, j - i), upper;
            for (char ch : word) upper += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            tokens.push_back(upper == "AND" || upper == "OR" ? upper : word);
            i = j;
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            std::size_t j = i;
            while (j < q.size() && std::isdigit(static_cast<unsigned char>(q[j]))) ++j;
            tokens.push_back(q.substr(i, j - i));
            i = j;
        } else if ((c == '<' || c == '>' || c == '!') && i + 1 < q.size() && q[i + 1] == '=') {
            tokens.push_back(q.substr(i, 2));
            i += 2;
        } else if (c == '&' && i + 1 < q.size() && q[i + 1] == '&') {
            tokens.push_back("AND");
            i += 2;
        } else if (c == '|' && i + 1 < q.size() && q[i + 1] == '|') {
            tokens.push_back("OR");
            i += 2;
        } else {
            tokens.push_back(std::string(1, c));
            ++i;
        }
    }
    return tokens;
}

std::vector<std::string> toRPN(const std::vector<std::string> &tokens) {
    std::vector<std::string> output;
    std::stack<std::string> ops;
    for (const auto &tok : tokens) {
        if (tok == "(") {
            ops.push(tok);
        } else if (tok == ")") {
            while (!ops.empty() && ops.top() != "(") {
                output.push_back(ops.top());
                ops.pop();
            }
            if (ops.empty()) throw std::runtime_error("unbalanced ')'");
            ops.pop();
        } else if (isOperator(tok)) {
            // all operators here are left-associative
            while (!ops.empty() && ops.top() != "(" && precedence(ops.top()) >= precedence(tok)) {
                output.push_back(ops.top());
                ops.pop();
            }
            ops.push(tok);
        } else {
            output.push_back(tok);
        }
    }
    while (!ops.empty()) {
        if (ops.top() == "(") throw std::runtime_error("unbalanced '('");
        output.push_back(ops.top());
        ops.pop();
    }
    return output;
}

bool evalRPN(const std::vector<std::string> &rpn, const Row &r) {
    std::stack<int> st;
    for (const auto &tok : rpn) {
        if (!isOperator(tok)) {
            st.push(isNumber(tok) ? std::stoi(tok) : column(tok, r));
            continue;
        }
        if (st.size() < 2) throw std::runtime_error("malformed expression");
        int b = st.top(); st.pop();
        int a = st.top(); st.pop();
        if (tok == ">")       st.push(a > b);
        else if (tok == "<")  st.push(a < b);
        else if (tok == ">=") st.push(a >= b);
        else if (tok == "<=") st.push(a <= b);
        else if (tok == "=")  st.push(a == b);
        else if (tok == "!=") st.push(a != b);
        else if (tok == "AND") st.push(a && b);
        else                   st.push(a || b);
    }
    if (st.size() != 1) throw std::runtime_error("malformed expression");
    return st.top() != 0;
}

int main() {
    const std::vector<Row> rows = {
        {"Asha", 1, 19}, {"Rohan", 2, 20}, {"Kabir", 3, 19}, {"Sara", 4, 21},
        {"Vivaan", 5, 20}, {"Ira", 6, 31}, {"Neel", 7, 22}, {"Diya", 8, 33},
    };
    const std::string where = "id > 3 AND (age < 25 OR age >= 30)";

    try {
        auto rpn = toRPN(lex(where));
        std::cout << "WHERE " << where << "\nRPN:  ";
        for (const auto &t : rpn) std::cout << t << ' ';
        std::cout << "\n\nMatching rows:\n";
        for (const auto &r : rows)
            if (evalRPN(rpn, r))
                std::cout << "  id=" << r.id << "  age=" << r.age << "  name=" << r.name << '\n';
    } catch (const std::exception &e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
