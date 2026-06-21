#include <cctype>
#include <cmath>
#include <iostream>
#include <stack>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

using namespace std;

struct OperatorInfo {
    int precedence;
    bool rightAssociative;
};

const unordered_map<string, OperatorInfo> OPERATORS = {
    {"||", {1, false}}, {"&&", {2, false}}, {"=", {3, false}},
    {"!=", {3, false}}, {"<", {4, false}}, {">", {4, false}},
    {"<=", {4, false}}, {">=", {4, false}}, {"+", {5, false}},
    {"-", {5, false}}, {"*", {6, false}}, {"/", {6, false}},
    {"^", {7, true}}
};

string toUpper(string text) {
    for(char& ch : text) {
        ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
    }
    return text;
}

vector<string> tokenizeExpression(const string& expression) {
    vector<string> tokens;
    size_t index = 0;

    while(index < expression.size()) {
        unsigned char current = static_cast<unsigned char>(expression[index]);

        if(isspace(current)) {
            ++index;
            continue;
        }

        if(isdigit(current) ||
           (expression[index] == '.' && index + 1 < expression.size() &&
            isdigit(static_cast<unsigned char>(expression[index + 1])))) {
            size_t end = index;
            while(end < expression.size() &&
                  (isdigit(static_cast<unsigned char>(expression[end])) ||
                   expression[end] == '.')) {
                ++end;
            }
            tokens.push_back(expression.substr(index, end - index));
            index = end;
        }
        else if(isalpha(current) || expression[index] == '_') {
            size_t end = index;
            while(end < expression.size() &&
                  (isalnum(static_cast<unsigned char>(expression[end])) ||
                   expression[end] == '_')) {
                ++end;
            }

            string word = expression.substr(index, end - index);
            string upper = toUpper(word);
            if(upper == "AND") tokens.push_back("&&");
            else if(upper == "OR") tokens.push_back("||");
            else tokens.push_back(word);
            index = end;
        }
        else if(expression[index] == '(' || expression[index] == ')') {
            tokens.push_back(string(1, expression[index]));
            ++index;
        }
        else {
            if(index + 1 < expression.size()) {
                string twoCharacters = expression.substr(index, 2);
                if(OPERATORS.count(twoCharacters)) {
                    tokens.push_back(twoCharacters);
                    index += 2;
                    continue;
                }
            }

            string oneCharacter(1, expression[index]);
            if(!OPERATORS.count(oneCharacter)) {
                throw runtime_error("Unknown token: " + oneCharacter);
            }
            tokens.push_back(oneCharacter);
            ++index;
        }
    }

    return tokens;
}

vector<string> toRpn(const vector<string>& tokens) {
    vector<string> output;
    stack<string> operators;

    for(const string& token : tokens) {
        if(token == "(") {
            operators.push(token);
        }
        else if(token == ")") {
            while(!operators.empty() && operators.top() != "(") {
                output.push_back(operators.top());
                operators.pop();
            }

            if(operators.empty()) throw runtime_error("Mismatched parentheses");
            operators.pop();
        }
        else if(OPERATORS.count(token)) {
            const OperatorInfo& current = OPERATORS.at(token);

            while(!operators.empty() && OPERATORS.count(operators.top())) {
                const OperatorInfo& top = OPERATORS.at(operators.top());
                bool shouldPop = top.precedence > current.precedence ||
                    (top.precedence == current.precedence &&
                     !current.rightAssociative);

                if(!shouldPop) break;
                output.push_back(operators.top());
                operators.pop();
            }
            operators.push(token);
        }
        else {
            output.push_back(token);
        }
    }

    while(!operators.empty()) {
        if(operators.top() == "(") throw runtime_error("Mismatched parentheses");
        output.push_back(operators.top());
        operators.pop();
    }

    return output;
}

double evaluateRpn(const vector<string>& rpn,
                   const unordered_map<string, double>& variables) {
    stack<double> values;

    for(const string& token : rpn) {
        if(OPERATORS.count(token)) {
            if(values.size() < 2) throw runtime_error("Invalid expression");

            double right = values.top();
            values.pop();
            double left = values.top();
            values.pop();

            if(token == "+") values.push(left + right);
            else if(token == "-") values.push(left - right);
            else if(token == "*") values.push(left * right);
            else if(token == "/") {
                if(right == 0) throw runtime_error("Division by zero");
                values.push(left / right);
            }
            else if(token == "^") values.push(pow(left, right));
            else if(token == "<") values.push(left < right ? 1.0 : 0.0);
            else if(token == ">") values.push(left > right ? 1.0 : 0.0);
            else if(token == "<=") values.push(left <= right ? 1.0 : 0.0);
            else if(token == ">=") values.push(left >= right ? 1.0 : 0.0);
            else if(token == "=") values.push(left == right ? 1.0 : 0.0);
            else if(token == "!=") values.push(left != right ? 1.0 : 0.0);
            else if(token == "&&") {
                values.push((left != 0 && right != 0) ? 1.0 : 0.0);
            }
            else if(token == "||") {
                values.push((left != 0 || right != 0) ? 1.0 : 0.0);
            }
        }
        else {
            try {
                size_t used = 0;
                double number = stod(token, &used);
                if(used != token.size()) throw invalid_argument("not a number");
                values.push(number);
            }
            catch(const exception&) {
                auto found = variables.find(token);
                if(found == variables.end()) {
                    throw runtime_error("Unknown variable: " + token);
                }
                values.push(found->second);
            }
        }
    }

    if(values.size() != 1) throw runtime_error("Invalid expression");
    return values.top();
}

void shuntingDemo() {
    string expression = "age * 2 + salary / 1000 > 100";
    vector<string> rpn = toRpn(tokenizeExpression(expression));
    unordered_map<string, double> variables = {{"age", 30}, {"salary", 50000}};

    cout << "Expression: " << expression << '\n';
    cout << "RPN: ";
    for(const string& token : rpn) cout << token << ' ';
    cout << "\nResult: "
         << (evaluateRpn(rpn, variables) != 0 ? "true" : "false")
         << "\n\n";
}