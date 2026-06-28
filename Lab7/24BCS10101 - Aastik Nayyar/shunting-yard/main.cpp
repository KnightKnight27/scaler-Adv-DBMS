#include <iostream>
#include <string>
#include <vector>
#include <stack>
#include <cctype>

using namespace std;

struct Token {
    string value;
    bool isOperator;
};

int getPrecedence(const string& op) {
    if (op == ">" ||
        op == "<" ||
        op == ">=" ||
        op == "<=" ||
        op == "=")
        return 3;

    if (op == "AND")
        return 2;

    if (op == "OR")
        return 1;

    return 0;
}

bool isOp(const string& tok) {
    return getPrecedence(tok) > 0;
}

vector<Token> tokenize(const string& expr) {

    vector<Token> tokens;
    int pos = 0;

    while (pos < expr.size()) {

        if (isspace(expr[pos])) {
            pos++;
            continue;
        }

        if (isalpha(expr[pos])) {

            string identifier;

            while (pos < expr.size() &&
                   (isalnum(expr[pos]) ||
                    expr[pos] == '_')) {

                identifier += expr[pos++];
            }

            string upper;

            for (char c : identifier)
                upper += toupper(c);

            if (upper == "AND" ||
                upper == "OR") {

                tokens.push_back(
                    {upper, true}
                );
            }
            else {
                tokens.push_back(
                    {identifier, false}
                );
            }
        }

        else if (isdigit(expr[pos])) {

            string numStr;

            while (pos < expr.size() &&
                   isdigit(expr[pos])) {

                numStr += expr[pos++];
            }

            tokens.push_back(
                {numStr, false}
            );
        }

        else if ((expr[pos] == '>' ||
                  expr[pos] == '<') &&
                 pos + 1 < expr.size() &&
                 expr[pos + 1] == '=') {

            string twoCharOp;

            twoCharOp += expr[pos];
            twoCharOp += '=';

            tokens.push_back(
                {twoCharOp, true}
            );

            pos += 2;
        }

        else if (expr[pos] == '>' ||
                 expr[pos] == '<' ||
                 expr[pos] == '=') {

            tokens.push_back(
                {string(1, expr[pos]), true}
            );

            pos++;
        }

        else if (expr[pos] == '(') {

            tokens.push_back(
                {"(", false}
            );

            pos++;
        }

        else if (expr[pos] == ')') {

            tokens.push_back(
                {")", false}
            );

            pos++;
        }

        else {
            pos++;
        }
    }

    return tokens;
}

vector<Token> shuntingYard(
    const vector<Token>& infix
) {

    vector<Token> output;
    stack<Token> opStack;

    for (const Token& tok : infix) {

        if (tok.value == "(") {

            opStack.push(tok);
        }

        else if (tok.value == ")") {

            while (!opStack.empty() &&
                   opStack.top().value != "(") {

                output.push_back(
                    opStack.top()
                );

                opStack.pop();
            }

            if (!opStack.empty())
                opStack.pop();
        }

        else if (tok.isOperator) {

            while (!opStack.empty() &&
                   opStack.top().value != "(" &&
                   getPrecedence(
                       opStack.top().value
                   ) >= getPrecedence(
                       tok.value
                   )) {

                output.push_back(
                    opStack.top()
                );

                opStack.pop();
            }

            opStack.push(tok);
        }

        else {

            output.push_back(tok);
        }
    }

    while (!opStack.empty()) {

        output.push_back(
            opStack.top()
        );

        opStack.pop();
    }

    return output;
}

struct Student {
    string studentName;
    int studentId;
    int studentAge;
};

int getFieldValue(
    const string& field,
    const Student& student
) {

    if (field == "id")
        return student.studentId;

    if (field == "age")
        return student.studentAge;

    return 0;
}

bool isNumeric(const string& str) {

    if (str.empty())
        return false;

    for (char c : str) {
        if (!isdigit(c))
            return false;
    }

    return true;
}

bool evaluateRPN(
    const vector<Token>& postfix,
    const Student& student
) {

    stack<int> stk;

    for (const Token& tok : postfix) {

        if (!tok.isOperator) {

            if (isNumeric(tok.value))
                stk.push(
                    stoi(tok.value)
                );
            else
                stk.push(
                    getFieldValue(
                        tok.value,
                        student
                    )
                );

            continue;
        }

        int rhs =
            stk.top();

        stk.pop();

        int lhs =
            stk.top();

        stk.pop();

        if (tok.value == ">")
            stk.push(
                lhs > rhs
            );

        else if (tok.value == "<")
            stk.push(
                lhs < rhs
            );

        else if (tok.value == ">=")
            stk.push(
                lhs >= rhs
            );

        else if (tok.value == "<=")
            stk.push(
                lhs <= rhs
            );

        else if (tok.value == "=")
            stk.push(
                lhs == rhs
            );

        else if (tok.value == "AND")
            stk.push(
                lhs && rhs
            );

        else if (tok.value == "OR")
            stk.push(
                lhs || rhs
            );
    }

    return stk.top();
}

int main() {

    string condition =
        "id > 3 AND (age < 25 OR age >= 30)";

    cout << "Input Expression:\n";
    cout << condition << "\n\n";

    vector<Token> tokens =
        tokenize(condition);

    vector<Token> postfix =
        shuntingYard(tokens);

    cout << "Postfix (RPN) Output:\n";

    for (const auto& tok : postfix)
        cout << tok.value << ' ';

    cout << "\n\n";

    vector<Student> students = {
        {"Arjun", 1, 21},
        {"Priya", 2, 23},
        {"Vikram", 3, 27},
        {"Sneha", 4, 25},
        {"Rahul", 5, 21},
        {"Ananya", 6, 32}
    };

    cout << "Filtered Results:\n";

    for (const auto& student : students) {

        if (evaluateRPN(
                postfix,
                student
            )) {

            cout
                << student.studentName
                << " (id="
                << student.studentId
                << ", age="
                << student.studentAge
                << ")\n";
        }
    }

    return 0;
}
