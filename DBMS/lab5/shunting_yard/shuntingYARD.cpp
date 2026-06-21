
#include <iostream>
#include <sstream>
#include <stack>
#include <vector>
#include <string>
#include <unordered_map>
#include <stdexcept>
#include <cctype>
#include <cmath>

struct OpInfo {
    int precedence;
    bool right_assoc;
};

const std::unordered_map<std::string, OpInfo> OPS = {
    {"||", {1, false}},
    {"&&", {2, false}},
    {"=",  {3, false}},
    {"!=", {3, false}},
    {"<",  {4, false}},
    {">",  {4, false}},
    {"<=", {4, false}},
    {">=", {4, false}},
    {"+",  {5, false}},
    {"-",  {5, false}},
    {"*",  {6, false}},
    {"/",  {6, false}},
    {"^",  {7, true}}
};

std::vector<std::string> tokenize(const std::string& expr) {
    std::vector<std::string> tokens;

    int i = 0;
    int n = expr.size();

    while (i < n) {

        if (std::isspace(expr[i])) {
            i++;
            continue;
        }

        if (std::isdigit(expr[i]) ||
            (expr[i] == '.' && i + 1 < n && std::isdigit(expr[i + 1]))) {

            int j = i;

            while (j < n &&
                  (std::isdigit(expr[j]) || expr[j] == '.'))
                j++;

            tokens.push_back(expr.substr(i, j - i));
            i = j;
        }
        else if (std::isalpha(expr[i]) || expr[i] == '_') {

            int j = i;

            while (j < n &&
                  (std::isalnum(expr[j]) || expr[j] == '_'))
                j++;

            tokens.push_back(expr.substr(i, j - i));
            i = j;
        }
        else if (expr[i] == '(' || expr[i] == ')') {
            tokens.push_back(std::string(1, expr[i]));
            i++;
        }
        else {

            if (i + 1 < n) {
                std::string two = expr.substr(i, 2);

                if (OPS.count(two)) {
                    tokens.push_back(two);
                    i += 2;
                    continue;
                }
            }

            tokens.push_back(std::string(1, expr[i]));
            i++;
        }
    }

    return tokens;
}

std::vector<std::string> to_rpn(
    const std::vector<std::string>& tokens) {

    std::vector<std::string> output;
    std::stack<std::string> operators;

    for (const auto& token : tokens) {

        if (token == "(") {
            operators.push(token);
        }

        else if (token == ")") {

            while (!operators.empty() &&
                   operators.top() != "(") {

                output.push_back(operators.top());
                operators.pop();
            }

            if (operators.empty())
                throw std::runtime_error(
                    "Mismatched Parentheses");

            operators.pop();
        }

        else if (OPS.count(token)) {

            auto current = OPS.at(token);

            while (!operators.empty() &&
                   OPS.count(operators.top())) {

                auto topOp = OPS.at(operators.top());

                if (topOp.precedence > current.precedence ||
                   (topOp.precedence == current.precedence &&
                    !current.right_assoc)) {

                    output.push_back(operators.top());
                    operators.pop();
                }
                else {
                    break;
                }
            }

            operators.push(token);
        }

        else {
            output.push_back(token);
        }
    }

    while (!operators.empty()) {

        if (operators.top() == "(")
            throw std::runtime_error(
                "Mismatched Parentheses");

        output.push_back(operators.top());
        operators.pop();
    }

    return output;
}

double eval_rpn(
    const std::vector<std::string>& rpn,
    const std::unordered_map<std::string, double>& vars) {

    std::stack<double> st;

    for (const auto& token : rpn) {

        if (OPS.count(token)) {

            double b = st.top();
            st.pop();

            double a = st.top();
            st.pop();

            if (token == "+") st.push(a + b);
            else if (token == "-") st.push(a - b);
            else if (token == "*") st.push(a * b);
            else if (token == "/") st.push(a / b);
            else if (token == "^") st.push(pow(a, b));

            else if (token == "<") st.push(a < b);
            else if (token == ">") st.push(a > b);
            else if (token == "<=") st.push(a <= b);
            else if (token == ">=") st.push(a >= b);

            else if (token == "=") st.push(a == b);
            else if (token == "!=") st.push(a != b);

            else if (token == "&&")
                st.push((a && b) ? 1.0 : 0.0);

            else if (token == "||")
                st.push((a || b) ? 1.0 : 0.0);
        }

        else {

            try {
                st.push(std::stod(token));
            }

            catch (...) {

                auto it = vars.find(token);

                if (it == vars.end())
                    throw std::runtime_error(
                        "Unknown variable: " + token);

                st.push(it->second);
            }
        }
    }

    return st.top();
}

void demo() {

    std::string expression =
        "age * 2 + salary / 1000 > 100";

    auto tokens = tokenize(expression);
    auto rpn = to_rpn(tokens);

    std::cout << "Expression : "
              << expression << "\n";

    std::cout << "RPN        : ";

    for (auto& token : rpn)
        std::cout << token << " ";

    std::cout << "\n";

    std::unordered_map<std::string, double> variables = {
        {"age", 30},
        {"salary", 50000}
    };

    bool result = eval_rpn(rpn, variables);

    std::cout << "Result     : "
              << (result ? "true" : "false")
              << "\n";
}

int main() {

    std::cout
        << "Shunting-Yard Algorithm Demo\n\n";

    demo();

    return 0;
}

