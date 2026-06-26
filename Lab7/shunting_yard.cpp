#include <cctype>
#include <cmath>
#include <iostream>
#include <stack>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

using namespace std;

struct PrecedenceRule {
    int rank;
    bool isRightToLeft;
};

const unordered_map<string, PrecedenceRule> OPMAP = {
    {"||", {1, false}}, {"&&", {2, false}}, {"=", {3, false}},
    {"!=", {3, false}}, {"<", {4, false}}, {">", {4, false}},
    {"<=", {4, false}}, {">=", {4, false}}, {"+", {5, false}},
    {"-", {5, false}}, {"*", {6, false}}, {"/", {6, false}},
    {"^", {7, true}}
};

string toUpper(string text) {
    for (size_t i = 0; i < text.length(); ++i) {
        text[i] = static_cast<char>(toupper(static_cast<unsigned char>(text[i])));
    }
    return text;
}

vector<string> tokenizeExpression(const string& expression) {
    vector<string> items;
    size_t cursor = 0;
    size_t length = expression.size();

    while (cursor < length) {
        unsigned char ch = static_cast<unsigned char>(expression[cursor]);

        if (isspace(ch)) {
            ++cursor;
            continue;
        }

        if (isdigit(ch) || (ch == '.' && cursor + 1 < length && isdigit(static_cast<unsigned char>(expression[cursor + 1])))) {
            size_t start = cursor;
            while (cursor < length && (isdigit(static_cast<unsigned char>(expression[cursor])) || expression[cursor] == '.')) {
                ++cursor;
            }
            items.push_back(expression.substr(start, cursor - start));
        }
        else if (isalpha(ch) || ch == '_') {
            size_t start = cursor;
            while (cursor < length && (isalnum(static_cast<unsigned char>(expression[cursor])) || expression[cursor] == '_')) {
                ++cursor;
            }
            string identifier = expression.substr(start, cursor - start);
            string normal = toUpper(identifier);
            if (normal == "AND") items.push_back("&&");
            else if (normal == "OR") items.push_back("||");
            else items.push_back(identifier);
        }
        else if (ch == '(' || ch == ')') {
            items.emplace_back(1, expression[cursor]);
            ++cursor;
        }
        else {
            if (cursor + 1 < length) {
                string pair = expression.substr(cursor, 2);
                if (OPMAP.find(pair) != OPMAP.end()) {
                    items.push_back(pair);
                    cursor += 2;
                    continue;
                }
            }
            string single(1, expression[cursor]);
            if (OPMAP.find(single) == OPMAP.end()) {
                throw runtime_error("Unknown token: " + single);
            }
            items.push_back(single);
            ++cursor;
        }
    }
    return items;
}

vector<string> toRpn(const vector<string>& tokens) {
    vector<string> postfix;
    stack<string> opStack;

    for (const auto& entry : tokens) {
        if (entry == ")") {
            while (!opStack.empty() && opStack.top() != "(") {
                postfix.push_back(opStack.top());
                opStack.pop();
            }
            if (opStack.empty()) throw runtime_error("Mismatched parentheses");
            opStack.pop();
        }
        else if (entry == "(") {
            opStack.push(entry);
        }
        else if (OPMAP.find(entry) != OPMAP.end()) {
            const auto& currentRule = OPMAP.at(entry);
            while (!opStack.empty() && OPMAP.find(opStack.top()) != OPMAP.end()) {
                const auto& topRule = OPMAP.at(opStack.top());
                if (topRule.rank > currentRule.rank || (topRule.rank == currentRule.rank && !currentRule.isRightToLeft)) {
                    postfix.push_back(opStack.top());
                    opStack.pop();
                } else {
                    break;
                }
            }
            opStack.push(entry);
        }
        else {
            postfix.push_back(entry);
        }
    }

    while (!opStack.empty()) {
        if (opStack.top() == "(") throw runtime_error("Mismatched parentheses");
        postfix.push_back(opStack.top());
        opStack.pop();
    }
    return postfix;
}

double evaluateRpn(const vector<string>& rpn, const unordered_map<string, double>& variables) {
    stack<double> operands;

    for (const auto& segment : rpn) {
        if (OPMAP.find(segment) != OPMAP.end()) {
            if (operands.size() < 2) throw runtime_error("Invalid expression");
            double rVal = operands.top(); operands.pop();
            double lVal = operands.top(); operands.pop();

            if (segment == "+") operands.push(lVal + rVal);
            else if (segment == "-") operands.push(lVal - rVal);
            else if (segment == "*") operands.push(lVal * rVal);
            else if (segment == "/") {
                if (rVal == 0) throw runtime_error("Division by zero");
                operands.push(lVal / rVal);
            }
            else if (segment == "^") operands.push(pow(lVal, rVal));
            else if (segment == "<") operands.push(lVal < rVal ? 1.0 : 0.0);
            else if (segment == ">") operands.push(lVal > rVal ? 1.0 : 0.0);
            else if (segment == "<=") operands.push(lVal <= rVal ? 1.0 : 0.0);
            else if (segment == ">=") operands.push(lVal >= rVal ? 1.0 : 0.0);
            else if (segment == "=") operands.push(lVal == rVal ? 1.0 : 0.0);
            else if (segment == "!=") operands.push(lVal != rVal ? 1.0 : 0.0);
            else if (segment == "&&") operands.push((lVal != 0.0 && rVal != 0.0) ? 1.0 : 0.0);
            else if (segment == "||") operands.push((lVal != 0.0 || rVal != 0.0) ? 1.0 : 0.0);
        }
        else {
            try {
                size_t parsedBytes = 0;
                double numeric = stod(segment, &parsedBytes);
                if (parsedBytes != segment.size()) throw invalid_argument("Bad conversion");
                operands.push(numeric);
            }
            catch (const exception&) {
                auto lookup = variables.find(segment);
                if (lookup == variables.end()) throw runtime_error("Unknown variable: " + segment);
                operands.push(lookup->second);
            }
        }
    }
    if (operands.size() != 1) throw runtime_error("Invalid expression");
    return operands.top();
}

void shuntingDemo() {
    string expr = "age * 2 + salary / 1000 > 100";
    vector<string> rpnForm = toRpn(tokenizeExpression(expr));
    unordered_map<string, double> environment = {{"age", 30}, {"salary", 50000}};

    cout << "Expression: " << expr << '\n';
    cout << "RPN: ";
    for (const auto& s : rpnForm) cout << s << ' ';
    cout << "\nResult: " << (evaluateRpn(rpnForm, environment) != 0.0 ? "true" : "false") << "\n\n";
}