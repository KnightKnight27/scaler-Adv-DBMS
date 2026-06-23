#include <iostream>
#include <stack>
#include <vector>
#include <unordered_map>
#include <variant>
#include <sstream>

using namespace std;

using Value = variant<int, bool>;

unordered_map<string, int> precedence = {
    {"OR", 0},
    {"AND", 1},
    {"=", 2}, {"!=", 2},
    {"<", 2}, {"<=", 2},
    {">", 2}, {">=", 2},
    {"+", 3}, {"-", 3},
    {"*", 4}, {"/", 4}, {"%", 4},
    {"NOT", 5}
};

bool isOperator(const string& token) {
    return precedence.count(token);
}

bool isNumber(const string& s) {
    if (s.empty()) return false;

    int start = (s[0] == '-') ? 1 : 0;

    for (int i = start; i < s.size(); ++i) {
        if (!isdigit(s[i]))
            return false;
    }

    return true;
}

vector<string> shuntingYard(const vector<string>& tokens) {
    vector<string> output;
    stack<string> ops;

    for (const string& token : tokens) {

        if (token == "(") {
            ops.push(token);
        }
        else if (token == ")") {

            while (!ops.empty() && ops.top() != "(") {
                output.push_back(ops.top());
                ops.pop();
            }

            if (!ops.empty())
                ops.pop();
        }
        else if (isOperator(token)) {

            while (!ops.empty() &&
                   ops.top() != "(" &&
                   precedence[ops.top()] >= precedence[token]) {
                output.push_back(ops.top());
                ops.pop();
            }

            ops.push(token);
        }
        else {
            output.push_back(token);
        }
    }

    while (!ops.empty()) {
        output.push_back(ops.top());
        ops.pop();
    }

    return output;
}

Value getOperand(
    const string& token,
    const unordered_map<string, Value>& variables)
{
    if (token == "true")
        return true;

    if (token == "false")
        return false;

    if (isNumber(token))
        return stoi(token);

    return variables.at(token);
}

Value applyBinary(const string& op, Value a, Value b) {

    if (op == "AND")
        return get<bool>(a) && get<bool>(b);

    if (op == "OR")
        return get<bool>(a) || get<bool>(b);

    int x = get<int>(a);
    int y = get<int>(b);

    if (op == "+") return x + y;
    if (op == "-") return x - y;
    if (op == "*") return x * y;
    if (op == "/") return x / y;
    if (op == "%") return x % y;

    if (op == ">") return x > y;
    if (op == "<") return x < y;
    if (op == ">=") return x >= y;
    if (op == "<=") return x <= y;
    if (op == "=") return x == y;
    if (op == "!=") return x != y;

    throw runtime_error("Unknown operator");
}

Value evaluatePostfix(
    const vector<string>& postfix,
    const unordered_map<string, Value>& variables)
{
    stack<Value> st;

    for (const string& token : postfix) {

        if (!isOperator(token)) {
            st.push(getOperand(token, variables));
        }
        else if (token == "NOT") {

            bool x = get<bool>(st.top());
            st.pop();

            st.push(!x);
        }
        else {
            Value right = st.top();
            st.pop();

            Value left = st.top();
            st.pop();

            st.push(applyBinary(token, left, right));
        }
    }

    return st.top();
}

int main() {

    // (age > 18 AND salary > 50000) OR admin = true

    vector<string> tokens = {
        "(",
        "age", ">", "18",
        "AND",
        "salary", ">", "50000",
        ")",
        "OR",
        "admin", "=", "true"
    };

    unordered_map<string, Value> row = {
        {"age", 25},
        {"salary", 40000},
        {"admin", false}
    };

    vector<string> postfix = shuntingYard(tokens);

    cout << "Postfix:\n";
    for (auto& t : postfix)
        cout << t << " ";
    cout << "\n\n";

    Value result = evaluatePostfix(postfix, row);

    cout << "Result: "
         << (get<bool>(result) ? "TRUE" : "FALSE")
         << "\n";
}
