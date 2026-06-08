// Lab 7 — Dijkstra's Shunting-Yard Algorithm
// Author: 24BCS10183 Aman Yadav  (Class B, 2nd year)
//
// Converts a SQL WHERE clause written in INFIX notation into POSTFIX (RPN),
// honouring operator precedence and parentheses, then evaluates the postfix
// against a list of rows to filter them.
//
//   Infix:    id > 3 AND (age < 25 OR age >= 30)
//   Postfix:  id 3 > age 25 < age 30 >= OR AND
//
// Postfix has no parentheses: the order of operators already encodes
// precedence, so a machine can evaluate it left-to-right with a single stack.

#include <cctype>
#include <iostream>
#include <stack>
#include <string>
#include <vector>

using namespace std;

struct Token {
    string text;
    bool   isOperator;
};

// Higher number = binds tighter (evaluated first).
//   comparisons  >  AND  >  OR
int precedence(const string& op) {
    if (op == ">" || op == "<" || op == ">=" || op == "<=" || op == "=") return 3;
    if (op == "AND") return 2;
    if (op == "OR")  return 1;
    return 0;
}

bool isOperatorToken(const string& t) { return precedence(t) > 0; }

// Break the WHERE string into operand / operator / parenthesis tokens.
vector<Token> tokenize(const string& input) {
    vector<Token> tokens;
    size_t pos = 0;
    while (pos < input.size()) {
        if (isspace(static_cast<unsigned char>(input[pos]))) { ++pos; continue; }

        // word: keyword (AND / OR) or column name
        if (isalpha(static_cast<unsigned char>(input[pos]))) {
            string word;
            while (pos < input.size() &&
                   (isalnum(static_cast<unsigned char>(input[pos])) ||
                    input[pos] == '_')) {
                word += input[pos++];
            }
            string upper;
            for (char c : word) upper += toupper(static_cast<unsigned char>(c));
            if (upper == "AND" || upper == "OR") tokens.push_back({upper, true});
            else                                 tokens.push_back({word,  false});
        }
        // number operand
        else if (isdigit(static_cast<unsigned char>(input[pos]))) {
            string num;
            while (pos < input.size() &&
                   isdigit(static_cast<unsigned char>(input[pos]))) {
                num += input[pos++];
            }
            tokens.push_back({num, false});
        }
        // two-char comparison operators >= and <=
        else if ((input[pos] == '>' || input[pos] == '<') &&
                 pos + 1 < input.size() && input[pos + 1] == '=') {
            tokens.push_back({string() + input[pos] + '=', true});
            pos += 2;
        }
        // single-char operators
        else if (input[pos] == '>' || input[pos] == '<' || input[pos] == '=') {
            tokens.push_back({string(1, input[pos]), true});
            ++pos;
        } else if (input[pos] == '(') {
            tokens.push_back({"(", false}); ++pos;
        } else if (input[pos] == ')') {
            tokens.push_back({")", false}); ++pos;
        } else {
            ++pos; // skip anything unexpected
        }
    }
    return tokens;
}

// Shunting-yard core: infix tokens -> postfix (RPN) tokens.
vector<Token> shuntingYard(const vector<Token>& tokens) {
    vector<Token> output;     // RPN result queue
    stack<Token>  operators;  // operator / paren holding stack

    for (const Token& tok : tokens) {
        if (tok.text == "(") {
            operators.push(tok);
        } else if (tok.text == ")") {
            // pop until the matching '('
            while (!operators.empty() && operators.top().text != "(") {
                output.push_back(operators.top());
                operators.pop();
            }
            if (!operators.empty()) operators.pop(); // discard the '('
        } else if (tok.isOperator) {
            // left-associative: pop while stack-top has greater-or-equal precedence
            while (!operators.empty() &&
                   operators.top().text != "(" &&
                   precedence(operators.top().text) >= precedence(tok.text)) {
                output.push_back(operators.top());
                operators.pop();
            }
            operators.push(tok);
        } else {
            // operand goes straight to output
            output.push_back(tok);
        }
    }

    while (!operators.empty()) {
        output.push_back(operators.top());
        operators.pop();
    }
    return output;
}

// ---------------------------------------------------------------------------
// Postfix evaluator — proves the conversion is correct by filtering rows.
// ---------------------------------------------------------------------------
struct Employee {
    string name;
    int    id;
    int    age;
};

int columnValue(const string& name, const Employee& row) {
    if (name == "id")  return row.id;
    if (name == "age") return row.age;
    return 0;
}

bool isNumber(const string& s) {
    for (char c : s) if (!isdigit(static_cast<unsigned char>(c))) return false;
    return !s.empty();
}

bool evaluatePostfix(const vector<Token>& postfix, const Employee& row) {
    stack<int> st; // comparison results are pushed as 0/1
    for (const Token& tok : postfix) {
        if (!tok.isOperator) {
            st.push(isNumber(tok.text) ? stoi(tok.text)
                                       : columnValue(tok.text, row));
            continue;
        }
        int b = st.top(); st.pop();
        int a = st.top(); st.pop();
        const string& op = tok.text;
        if      (op == ">")   st.push(a >  b);
        else if (op == "<")   st.push(a <  b);
        else if (op == ">=")  st.push(a >= b);
        else if (op == "<=")  st.push(a <= b);
        else if (op == "=")   st.push(a == b);
        else if (op == "AND") st.push(a && b);
        else if (op == "OR")  st.push(a || b);
    }
    return st.top();
}

int main() {
    string whereClause = "id > 3 AND (age < 25 OR age >= 30)";

    cout << "Infix WHERE:  " << whereClause << "\n";

    vector<Token> tokens  = tokenize(whereClause);
    vector<Token> postfix = shuntingYard(tokens);

    cout << "Postfix (RPN): ";
    for (const Token& t : postfix) cout << t.text << ' ';
    cout << "\n\n";

    vector<Employee> employees = {
        {"Aman",   1, 19},
        {"Riya",   2, 20},
        {"Karan",  3, 19},
        {"Sneha",  4, 21},
        {"Vivaan", 5, 20},
        {"Ishaan", 6, 31},
        {"Meera",  7, 22},
    };

    cout << "Rows matching the WHERE clause:\n";
    for (const Employee& row : employees) {
        if (evaluatePostfix(postfix, row))
            cout << "  " << row.name
                 << " (id=" << row.id << ", age=" << row.age << ")\n";
    }
    return 0;
}
