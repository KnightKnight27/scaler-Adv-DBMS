#include <cassert>
#include <iostream>
#include <stack>
#include <string>
#include <vector>

bool isDigit(char c) {
    return c >= '0' and c <= '9';
}

int get_priority(char c) {
    assert(c == '+' or c == '-' or c == '*' or c == '/');

    if (c == '+' or c == '-') {
        return 1;
    } else {
        return 2;
    }
}

std::vector<char> shunting_yard(const std::string &s) {
    std::stack<char> operator_stack;
    std::vector<char> res;

    for (char c : s) {
        if (isDigit(c)) {
            res.emplace_back(c);
        } else if (c == '(') {
            operator_stack.push(c);
        } else if (c == ')') {
            char oc = operator_stack.top();
            while (oc != '(') {
                res.emplace_back(oc);
                operator_stack.pop();
                oc = operator_stack.top();
            }
            operator_stack.pop();
        } else {
            while (true) {
                if (operator_stack.empty()) {
                    operator_stack.push(c);
                    break;
                } else {
                    char op = operator_stack.top();

                    if (op == '(') {
                        operator_stack.push(c);
                        break;
                    }

                    int our_priority = get_priority(c);
                    int op_priority = get_priority(op);

                    if (our_priority <= op_priority) {
                        operator_stack.pop();
                        res.emplace_back(op);
                    } else {
                        operator_stack.push(c);
                        break;
                    }
                }
            }
        }
    }

    while (!operator_stack.empty()) {
        res.emplace_back(operator_stack.top());
        operator_stack.pop();
    }

    return res;
}

int main() {
    std::string s;
    std::cin >> s;

    std::vector<char> res = shunting_yard(s);
    for (char c : res) {
        std::cout << c << ' ';
    }
    std::cout << '\n';
}
