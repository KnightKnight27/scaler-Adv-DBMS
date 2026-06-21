#include <iostream>
#include <stack>
#include <vector>
#include <sstream>
#include <cmath>

using namespace std;

int precedence(char op) {
    if (op == '+' || op == '-') return 1;
    if (op == '*' || op == '/') return 2;
    if (op == '^') return 3;
    return 0;
}

bool isOperator(char c) {
    return c=='+' || c=='-' || c=='*' || c=='/' || c=='^';
}

vector<string> infixToPostfix(string expr) {
    stack<char> ops;
    vector<string> output;

    for (size_t i = 0; i < expr.size(); i++) {
        char c = expr[i];

        if (c == ' ')
            continue;

        if (isdigit(c)) {
            string num;
            while (i < expr.size() && isdigit(expr[i])) {
                num += expr[i];
                i++;
            }
            i--;
            output.push_back(num);
        }
        else if (c == '(') {
            ops.push(c);
        }
        else if (c == ')') {
            while (!ops.empty() && ops.top() != '(') {
                output.push_back(string(1, ops.top()));
                ops.pop();
            }
            ops.pop();
        }
        else if (isOperator(c)) {
            while (!ops.empty() &&
                   precedence(ops.top()) >= precedence(c)) {
                output.push_back(string(1, ops.top()));
                ops.pop();
            }
            ops.push(c);
        }
    }

    while (!ops.empty()) {
        output.push_back(string(1, ops.top()));
        ops.pop();
    }

    return output;
}

double evaluatePostfix(vector<string>& postfix) {
    stack<double> st;

    for (auto token : postfix) {
        if (isdigit(token[0])) {
            st.push(stod(token));
        } else {
            double b = st.top(); st.pop();
            double a = st.top(); st.pop();

            switch(token[0]) {
                case '+': st.push(a+b); break;
                case '-': st.push(a-b); break;
                case '*': st.push(a*b); break;
                case '/': st.push(a/b); break;
                case '^': st.push(pow(a,b)); break;
            }
        }
    }

    return st.top();
}

int main() {
    string expr = "3 + 4 * 2 / ( 1 - 5 )";

    auto postfix = infixToPostfix(expr);

    cout << "Postfix: ";
    for (auto &t : postfix)
        cout << t << " ";

    cout << "\nResult: "
         << evaluatePostfix(postfix)
         << endl;

    return 0;
}