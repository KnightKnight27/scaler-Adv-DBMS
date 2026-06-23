#include <iostream>
#include <string>
#include <vector>
#include <stack>
#include <cctype>

using namespace std;

// ---------------------------------------------------------------------------
// Dijkstra's Shunting-Yard Algorithm
//
// Converts a SQL WHERE clause written in INFIX notation into POSTFIX (RPN)
// notation, honouring operator precedence and parentheses.
//
//   Infix:    id > 3 AND (age < 25 OR age >= 30)
//   Postfix:  id 3 > age 25 < age 30 >= OR AND
//
// Postfix needs no parentheses: the order of operators already encodes
// precedence, so a machine can evaluate it left-to-right with a single stack.
// ---------------------------------------------------------------------------

struct Token {
    string text;
    bool isOperator;
};

// Higher number = binds tighter (evaluated first).
//   comparisons  >  AND  >  OR
int precedence(const string& op) {
    if (op == ">" || op == "<" || op == ">=" || op == "<=" || op == "=")
        return 3;
    if (op == "AND")
        return 2;
    if (op == "OR")
        return 1;
    return 0;
}

bool isOperatorToken(const string& t) {
    return precedence(t) > 0;
}

// Break the WHERE string into operand / operator / parenthesis tokens.
vector<Token> tokenize(const string& input) {
    vector<Token> tokens;
    int pos = 0;
    while (pos < (int)input.size()) {
        if (isspace(input[pos])) {
            ++pos;
            continue;
        }

        // word: keyword (AND / OR) or column name
        if (isalpha(input[pos])) {
            string word;
            while (pos < (int)input.size() &&
                   (isalnum(input[pos]) || input[pos] == '_')) {
                word += input[pos++];
            }
            string upper;
            for (char c : word) upper += toupper(c);
            if (upper == "AND" || upper == "OR")
                tokens.push_back({upper, true});
            else
                tokens.push_back({word, false}); // column name = operand
        }
        // number operand
        else if (isdigit(input[pos])) {
            string num;
            while (pos < (int)input.size() && isdigit(input[pos]))
                num += input[pos++];
            tokens.push_back({num, false});
        }
        // two-char comparison operators >= and <=
        else if ((input[pos] == '>' || input[pos] == '<') &&
                 pos + 1 < (int)input.size() && input[pos + 1] == '=') {
            tokens.push_back({string() + input[pos] + '=', true});
            pos += 2;
        }
        // single-char operators and parentheses
        else if (input[pos] == '>' || input[pos] == '<' || input[pos] == '=') {
            tokens.push_back({string(1, input[pos]), true});
            ++pos;
        } else if (input[pos] == '(') {
            tokens.push_back({"(", false});
            ++pos;
        } else if (input[pos] == ')') {
            tokens.push_back({")", false});
            ++pos;
        } else {
            ++pos; // skip anything unexpected
        }
    }
    return tokens;
}

// The shunting-yard core: infix tokens -> postfix (RPN) tokens.
vector<Token> shuntingYard(const vector<Token>& tokens) {
    vector<Token> output;     // the RPN result queue
    stack<Token> operators;   // operator / paren holding stack

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
            // all our operators are left-associative, so pop while the
            // operator on top has greater-or-equal precedence
            while (!operators.empty() &&
                   operators.top().text != "(" &&
                   precedence(operators.top().text) >= precedence(tok.text)) {
                output.push_back(operators.top());
                operators.pop();
            }
            operators.push(tok);
        } else {
            // operand (column name or number) goes straight to output
            output.push_back(tok);
        }
    }

    // drain remaining operators
    while (!operators.empty()) {
        output.push_back(operators.top());
        operators.pop();
    }
    return output;
}

// ---------------------------------------------------------------------------
// Bonus: evaluate the postfix against one row to prove the order is correct.
// ---------------------------------------------------------------------------
struct Employee {
    string name;
    int id;
    int age;
};

int columnValue(const string& name, const Employee& row) {
    if (name == "id")  return row.id;
    if (name == "age") return row.age;
    return 0;
}

bool isNumber(const string& s) {
    for (char c : s) if (!isdigit(c)) return false;
    return !s.empty();
}

bool evaluatePostfix(const vector<Token>& postfix, const Employee& row) {
    stack<int> st; // we push ints; comparison results are 0/1 booleans
    for (const Token& tok : postfix) {
        if (!tok.isOperator) {
            st.push(isNumber(tok.text) ? stoi(tok.text)
                                       : columnValue(tok.text, row));
            continue;
        }
        int b = st.top(); st.pop();
        int a = st.top(); st.pop();
        const string& op = tok.text;
        if (op == ">")       st.push(a > b);
        else if (op == "<")  st.push(a < b);
        else if (op == ">=") st.push(a >= b);
        else if (op == "<=") st.push(a <= b);
        else if (op == "=")  st.push(a == b);
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
        {"Ankur", 1, 22},
        {"Manjari", 2, 22},
        {"Kartik", 3, 28},
        {"Swapnanil", 4, 24},
        {"Nipu", 5, 22},
        {"Xiary", 6, 31},
    };

    cout << "Rows matching the WHERE clause:\n";
    for (const Employee& row : employees) {
        if (evaluatePostfix(postfix, row))
            cout << "  " << row.name << " (id=" << row.id
                 << ", age=" << row.age << ")\n";
    }

    return 0;
}
