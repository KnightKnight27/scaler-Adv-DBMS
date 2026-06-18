#include <iostream>
#include <string>
#include <vector>
#include <stack>
#include <cctype>

using namespace std;

struct TokenUnit {
    string value;
    bool isOperator;
};


int precedenceLevel(const string& op) {
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

bool operatorToken(const string& token) {
    return precedenceLevel(token) > 0;
}

vector<TokenUnit> tokenizeCondition(const string& expression) {

    vector<TokenUnit> tokens;
    int position = 0;

    while (position < expression.size()) {

        if (isspace(expression[position])) {
            position++;
            continue;
        }

        // identifiers and keywords
        if (isalpha(expression[position])) {

            string text;

            while (position < expression.size() &&
                   (isalnum(expression[position]) ||
                    expression[position] == '_')) {

                text += expression[position++];
            }

            string upperText;

            for (char ch : text)
                upperText += toupper(ch);

            if (upperText == "AND" ||
                upperText == "OR") {

                tokens.push_back(
                    {upperText, true}
                );
            }
            else {
                tokens.push_back(
                    {text, false}
                );
            }
        }

        // numeric constants
        else if (isdigit(expression[position])) {

            string number;

            while (position < expression.size() &&
                   isdigit(expression[position])) {

                number += expression[position++];
            }

            tokens.push_back(
                {number, false}
            );
        }

        // >= and <=
        else if ((expression[position] == '>' ||
                  expression[position] == '<') &&
                 position + 1 < expression.size() &&
                 expression[position + 1] == '=') {

            string compoundOperator;

            compoundOperator += expression[position];
            compoundOperator += '=';

            tokens.push_back(
                {compoundOperator, true}
            );

            position += 2;
        }

        // single-character operators
        else if (expression[position] == '>' ||
                 expression[position] == '<' ||
                 expression[position] == '=') {

            tokens.push_back(
                {string(1, expression[position]), true}
            );

            position++;
        }

        // left parenthesis
        else if (expression[position] == '(') {

            tokens.push_back(
                {"(", false}
            );

            position++;
        }

        // right parenthesis
        else if (expression[position] == ')') {

            tokens.push_back(
                {")", false}
            );

            position++;
        }

        else {
            position++;
        }
    }

    return tokens;
}


vector<TokenUnit> convertToPostfix(
    const vector<TokenUnit>& infixTokens
) {

    vector<TokenUnit> output;
    stack<TokenUnit> operatorStack;

    for (const TokenUnit& token : infixTokens) {

        if (token.value == "(") {

            operatorStack.push(token);
        }

        else if (token.value == ")") {

            while (!operatorStack.empty() &&
                   operatorStack.top().value != "(") {

                output.push_back(
                    operatorStack.top()
                );

                operatorStack.pop();
            }

            if (!operatorStack.empty())
                operatorStack.pop();
        }

        else if (token.isOperator) {

            while (!operatorStack.empty() &&
                   operatorStack.top().value != "(" &&
                   precedenceLevel(
                       operatorStack.top().value
                   ) >= precedenceLevel(
                       token.value
                   )) {

                output.push_back(
                    operatorStack.top()
                );

                operatorStack.pop();
            }

            operatorStack.push(token);
        }

        else {

            output.push_back(token);
        }
    }

    while (!operatorStack.empty()) {

        output.push_back(
            operatorStack.top()
        );

        operatorStack.pop();
    }

    return output;
}

struct EmployeeRecord {
    string name;
    int id;
    int age;
};

int getColumnValue(
    const string& column,
    const EmployeeRecord& record
) {

    if (column == "id")
        return record.id;

    if (column == "age")
        return record.age;

    return 0;
}

bool numericLiteral(const string& text) {

    if (text.empty())
        return false;

    for (char ch : text) {
        if (!isdigit(ch))
            return false;
    }

    return true;
}

bool evaluatePostfix(
    const vector<TokenUnit>& postfix,
    const EmployeeRecord& record
) {

    stack<int> evaluationStack;

    for (const TokenUnit& token : postfix) {

        if (!token.isOperator) {

            if (numericLiteral(token.value))
                evaluationStack.push(
                    stoi(token.value)
                );
            else
                evaluationStack.push(
                    getColumnValue(
                        token.value,
                        record
                    )
                );

            continue;
        }

        int rightOperand =
            evaluationStack.top();

        evaluationStack.pop();

        int leftOperand =
            evaluationStack.top();

        evaluationStack.pop();

        if (token.value == ">")
            evaluationStack.push(
                leftOperand > rightOperand
            );

        else if (token.value == "<")
            evaluationStack.push(
                leftOperand < rightOperand
            );

        else if (token.value == ">=")
            evaluationStack.push(
                leftOperand >= rightOperand
            );

        else if (token.value == "<=")
            evaluationStack.push(
                leftOperand <= rightOperand
            );

        else if (token.value == "=")
            evaluationStack.push(
                leftOperand == rightOperand
            );

        else if (token.value == "AND")
            evaluationStack.push(
                leftOperand && rightOperand
            );

        else if (token.value == "OR")
            evaluationStack.push(
                leftOperand || rightOperand
            );
    }

    return evaluationStack.top();
}

int main() {

    string whereClause =
        "id > 3 AND (age < 25 OR age >= 30)";

    cout << "Original Expression:\n";
    cout << whereClause << "\n\n";

    vector<TokenUnit> tokens =
        tokenizeCondition(whereClause);

    vector<TokenUnit> postfix =
        convertToPostfix(tokens);

    cout << "Generated Postfix:\n";

    for (const auto& token : postfix)
        cout << token.value << ' ';

    cout << "\n\n";

    vector<EmployeeRecord> employees = {
        {"Aarav", 1, 22},
        {"Diya", 2, 22},
        {"Rohan", 3, 28},
        {"Meera", 4, 24},
        {"Kabir", 5, 22},
        {"Ishita", 6, 31}
    };

    cout << "Matching Records:\n";

    for (const auto& employee : employees) {

        if (evaluatePostfix(
                postfix,
                employee
            )) {

            cout
                << employee.name
                << " (id="
                << employee.id
                << ", age="
                << employee.age
                << ")\n";
        }
    }

    return 0;
}