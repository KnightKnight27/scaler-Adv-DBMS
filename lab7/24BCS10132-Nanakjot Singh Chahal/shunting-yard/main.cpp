#include <iostream>
#include <string>
#include <vector>
#include <stack>
#include <cctype>

using namespace std;

struct Symbol {
    string content;
    bool isOp;
};

int opPriority(const string& oper) {
    if (oper == ">" ||
        oper == "<" ||
        oper == ">=" ||
        oper == "<=" ||
        oper == "=")
        return 3;

    if (oper == "AND")
        return 2;

    if (oper == "OR")
        return 1;

    return 0;
}

bool isOperatorSymbol(const string& sym) {
    return opPriority(sym) > 0;
}

vector<Symbol> extractSymbols(const string& clause) {

    vector<Symbol> symbols;
    int cursor = 0;

    while (cursor < clause.size()) {

        if (isspace(clause[cursor])) {
            cursor++;
            continue;
        }

        if (isalpha(clause[cursor])) {

            string word;

            while (cursor < clause.size() &&
                   (isalnum(clause[cursor]) ||
                    clause[cursor] == '_')) {

                word += clause[cursor++];
            }

            string uppercased;

            for (char c : word)
                uppercased += toupper(c);

            if (uppercased == "AND" ||
                uppercased == "OR") {

                symbols.push_back(
                    {uppercased, true}
                );
            }
            else {
                symbols.push_back(
                    {word, false}
                );
            }
        }

        else if (isdigit(clause[cursor])) {

            string digits;

            while (cursor < clause.size() &&
                   isdigit(clause[cursor])) {

                digits += clause[cursor++];
            }

            symbols.push_back(
                {digits, false}
            );
        }

        else if ((clause[cursor] == '>' ||
                  clause[cursor] == '<') &&
                 cursor + 1 < clause.size() &&
                 clause[cursor + 1] == '=') {

            string twoChar;

            twoChar += clause[cursor];
            twoChar += '=';

            symbols.push_back(
                {twoChar, true}
            );

            cursor += 2;
        }

        else if (clause[cursor] == '>' ||
                 clause[cursor] == '<' ||
                 clause[cursor] == '=') {

            symbols.push_back(
                {string(1, clause[cursor]), true}
            );

            cursor++;
        }

        else if (clause[cursor] == '(') {

            symbols.push_back(
                {"(", false}
            );

            cursor++;
        }

        else if (clause[cursor] == ')') {

            symbols.push_back(
                {")", false}
            );

            cursor++;
        }

        else {
            cursor++;
        }
    }

    return symbols;
}

vector<Symbol> toRPN(
    const vector<Symbol>& infixSymbols
) {

    vector<Symbol> result;
    stack<Symbol> holdingStack;

    for (const Symbol& sym : infixSymbols) {

        if (sym.content == "(") {

            holdingStack.push(sym);
        }

        else if (sym.content == ")") {

            while (!holdingStack.empty() &&
                   holdingStack.top().content != "(") {

                result.push_back(
                    holdingStack.top()
                );

                holdingStack.pop();
            }

            if (!holdingStack.empty())
                holdingStack.pop();
        }

        else if (sym.isOp) {

            while (!holdingStack.empty() &&
                   holdingStack.top().content != "(" &&
                   opPriority(
                       holdingStack.top().content
                   ) >= opPriority(
                       sym.content
                   )) {

                result.push_back(
                    holdingStack.top()
                );

                holdingStack.pop();
            }

            holdingStack.push(sym);
        }

        else {

            result.push_back(sym);
        }
    }

    while (!holdingStack.empty()) {

        result.push_back(
            holdingStack.top()
        );

        holdingStack.pop();
    }

    return result;
}

struct StudentRow {
    string name;
    int id;
    int age;
};

int readField(
    const string& field,
    const StudentRow& row
) {

    if (field == "id")
        return row.id;

    if (field == "age")
        return row.age;

    return 0;
}

bool isIntegerString(const string& str) {

    if (str.empty())
        return false;

    for (char c : str) {
        if (!isdigit(c))
            return false;
    }

    return true;
}

bool computeRPN(
    const vector<Symbol>& rpn,
    const StudentRow& row
) {

    stack<int> calcStack;

    for (const Symbol& sym : rpn) {

        if (!sym.isOp) {

            if (isIntegerString(sym.content))
                calcStack.push(
                    stoi(sym.content)
                );
            else
                calcStack.push(
                    readField(
                        sym.content,
                        row
                    )
                );

            continue;
        }

        int rhs =
            calcStack.top();

        calcStack.pop();

        int lhs =
            calcStack.top();

        calcStack.pop();

        if (sym.content == ">")
            calcStack.push(
                lhs > rhs
            );

        else if (sym.content == "<")
            calcStack.push(
                lhs < rhs
            );

        else if (sym.content == ">=")
            calcStack.push(
                lhs >= rhs
            );

        else if (sym.content == "<=")
            calcStack.push(
                lhs <= rhs
            );

        else if (sym.content == "=")
            calcStack.push(
                lhs == rhs
            );

        else if (sym.content == "AND")
            calcStack.push(
                lhs && rhs
            );

        else if (sym.content == "OR")
            calcStack.push(
                lhs || rhs
            );
    }

    return calcStack.top();
}

int main() {

    string condition =
        "id > 3 AND (age < 25 OR age >= 30)";

    cout << "Input Expression:\n";
    cout << condition << "\n\n";

    vector<Symbol> symbols =
        extractSymbols(condition);

    vector<Symbol> rpn =
        toRPN(symbols);

    cout << "Postfix (RPN) Output:\n";

    for (const auto& sym : rpn)
        cout << sym.content << ' ';

    cout << "\n\n";

    vector<StudentRow> students = {
        {"Arjun", 1, 21},
        {"Priya", 2, 23},
        {"Vikram", 3, 27},
        {"Sneha", 4, 25},
        {"Rahul", 5, 21},
        {"Ananya", 6, 32}
    };

    cout << "Filtered Results:\n";

    for (const auto& student : students) {

        if (computeRPN(
                rpn,
                student
            )) {

            cout
                << student.name
                << " (id="
                << student.id
                << ", age="
                << student.age
                << ")\n";
        }
    }

    return 0;
}
