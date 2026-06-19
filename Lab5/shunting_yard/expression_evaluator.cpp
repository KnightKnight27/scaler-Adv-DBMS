#include <iostream>
#include <stack>
#include <string>
#include <cctype>

int precedence(char op) {
    if (op == '+' || op == '-')
        return 1;

    if (op == '*' || op == '/')
        return 2;

    return 0;
}

std::string infixToPostfix(const std::string& expression) {

    std::stack<char> operators;
    std::string postfix;

    for (char ch : expression) {

        if (ch == ' ')
            continue;

        if (std::isdigit(ch)) {

            postfix += ch;
            postfix += ' ';
        }

        else if (ch == '(') {
            operators.push(ch);
        }

        else if (ch == ')') {

            while (!operators.empty() &&
                   operators.top() != '(') {

                postfix += operators.top();
                postfix += ' ';

                operators.pop();
            }

            if (!operators.empty())
                operators.pop();
        }

        else {

            while (!operators.empty() &&
                   precedence(operators.top()) >= precedence(ch)) {

                postfix += operators.top();
                postfix += ' ';

                operators.pop();
            }

            operators.push(ch);
        }
    }

    while (!operators.empty()) {

        postfix += operators.top();
        postfix += ' ';

        operators.pop();
    }

    return postfix;
}

int evaluatePostfix(const std::string& postfix) {

    std::stack<int> values;

    for (char ch : postfix) {

        if (ch == ' ')
            continue;

        if (std::isdigit(ch)) {

            values.push(ch - '0');
        }

        else {

            int right = values.top();
            values.pop();

            int left = values.top();
            values.pop();

            switch (ch) {

                case '+':
                    values.push(left + right);
                    break;

                case '-':
                    values.push(left - right);
                    break;

                case '*':
                    values.push(left * right);
                    break;

                case '/':
                    values.push(left / right);
                    break;
            }
        }
    }

    return values.top();
}

int main() {

    std::string expression =
        "(3+4)*2";

    std::cout
        << "Infix Expression: "
        << expression
        << std::endl;

    std::string postfix =
        infixToPostfix(expression);

    std::cout
        << "Postfix Expression: "
        << postfix
        << std::endl;

    std::cout
        << "Result: "
        << evaluatePostfix(postfix)
        << std::endl;

    return 0;
}