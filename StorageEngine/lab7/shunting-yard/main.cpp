#include <iostream>
#include <string>
#include <vector>
#include <stack>
#include <cctype>

using namespace std;

// ---------------------------------------------------------------------------
// Dijkstra's Shunting-Yard Algorithm
//
// Rewrites a SQL WHERE clause given in INFIX form into POSTFIX (RPN) form,
// respecting operator precedence and parentheses.
//
//   Infix:    id > 3 AND (age < 25 OR age >= 30)
//   Postfix:  id 3 > age 25 < age 30 >= OR AND
//
// Postfix drops the parentheses entirely: operator ordering alone encodes
// precedence, so a machine can evaluate it in one left-to-right scan with a
// single stack.
// ---------------------------------------------------------------------------

struct Lexeme {
    string symbol;
    bool isOp;
};

// Bigger number = binds tighter (runs sooner).
//   comparisons  >  AND  >  OR
int rankOf(const string& sym) {
    if (sym == ">" || sym == "<" || sym == ">=" || sym == "<=" || sym == "=")
        return 3;
    if (sym == "AND")
        return 2;
    if (sym == "OR")
        return 1;
    return 0;
}

bool looksLikeOperator(const string& s) {
    return rankOf(s) > 0;
}

// Slice the WHERE string into operand / operator / parenthesis lexemes.
vector<Lexeme> splitIntoLexemes(const string& src) {
    vector<Lexeme> pieces;
    int cursor = 0;
    while (cursor < (int)src.size()) {
        if (isspace(src[cursor])) {
            ++cursor;
            continue;
        }

        // word: keyword (AND / OR) or column name
        if (isalpha(src[cursor])) {
            string word;
            while (cursor < (int)src.size() &&
                   (isalnum(src[cursor]) || src[cursor] == '_')) {
                word += src[cursor++];
            }
            string caps;
            for (char ch : word) caps += toupper(ch);
            if (caps == "AND" || caps == "OR")
                pieces.push_back({caps, true});
            else
                pieces.push_back({word, false}); // column name = operand
        }
        // number operand
        else if (isdigit(src[cursor])) {
            string digits;
            while (cursor < (int)src.size() && isdigit(src[cursor]))
                digits += src[cursor++];
            pieces.push_back({digits, false});
        }
        // two-char comparison operators >= and <=
        else if ((src[cursor] == '>' || src[cursor] == '<') &&
                 cursor + 1 < (int)src.size() && src[cursor + 1] == '=') {
            pieces.push_back({string() + src[cursor] + '=', true});
            cursor += 2;
        }
        // single-char operators and parentheses
        else if (src[cursor] == '>' || src[cursor] == '<' || src[cursor] == '=') {
            pieces.push_back({string(1, src[cursor]), true});
            ++cursor;
        } else if (src[cursor] == '(') {
            pieces.push_back({"(", false});
            ++cursor;
        } else if (src[cursor] == ')') {
            pieces.push_back({")", false});
            ++cursor;
        } else {
            ++cursor; // skip anything unexpected
        }
    }
    return pieces;
}

// The shunting-yard core: infix lexemes -> postfix (RPN) lexemes.
vector<Lexeme> toReversePolish(const vector<Lexeme>& pieces) {
    vector<Lexeme> rpnQueue;   // the RPN result queue
    stack<Lexeme> opStack;     // operator / paren holding stack

    for (const Lexeme& piece : pieces) {
        if (piece.symbol == "(") {
            opStack.push(piece);
        } else if (piece.symbol == ")") {
            // pop until the matching '('
            while (!opStack.empty() && opStack.top().symbol != "(") {
                rpnQueue.push_back(opStack.top());
                opStack.pop();
            }
            if (!opStack.empty()) opStack.pop(); // discard the '('
        } else if (piece.isOp) {
            // every operator here is left-associative, so pop while the
            // operator on top has greater-or-equal rank
            while (!opStack.empty() &&
                   opStack.top().symbol != "(" &&
                   rankOf(opStack.top().symbol) >= rankOf(piece.symbol)) {
                rpnQueue.push_back(opStack.top());
                opStack.pop();
            }
            opStack.push(piece);
        } else {
            // operand (column name or number) goes straight to the queue
            rpnQueue.push_back(piece);
        }
    }

    // drain remaining operators
    while (!opStack.empty()) {
        rpnQueue.push_back(opStack.top());
        opStack.pop();
    }
    return rpnQueue;
}

// ---------------------------------------------------------------------------
// Bonus: evaluate the postfix against one row to prove the order is correct.
// ---------------------------------------------------------------------------
struct StaffRow {
    string fullName;
    int recordId;
    int years;
};

int fieldValue(const string& field, const StaffRow& row) {
    if (field == "id")  return row.recordId;
    if (field == "age") return row.years;
    return 0;
}

bool isNumeric(const string& s) {
    for (char ch : s) if (!isdigit(ch)) return false;
    return !s.empty();
}

bool runPostfix(const vector<Lexeme>& rpn, const StaffRow& row) {
    stack<int> work; // we push ints; comparison results are 0/1 booleans
    for (const Lexeme& piece : rpn) {
        if (!piece.isOp) {
            work.push(isNumeric(piece.symbol) ? stoi(piece.symbol)
                                              : fieldValue(piece.symbol, row));
            continue;
        }
        int rhs = work.top(); work.pop();
        int lhs = work.top(); work.pop();
        const string& sym = piece.symbol;
        if (sym == ">")       work.push(lhs > rhs);
        else if (sym == "<")  work.push(lhs < rhs);
        else if (sym == ">=") work.push(lhs >= rhs);
        else if (sym == "<=") work.push(lhs <= rhs);
        else if (sym == "=")  work.push(lhs == rhs);
        else if (sym == "AND") work.push(lhs && rhs);
        else if (sym == "OR")  work.push(lhs || rhs);
    }
    return work.top();
}

int main() {
    string filterText = "id > 3 AND (age < 25 OR age >= 30)";

    cout << "Infix WHERE:  " << filterText << "\n";

    vector<Lexeme> pieces = splitIntoLexemes(filterText);
    vector<Lexeme> rpn    = toReversePolish(pieces);

    cout << "Postfix (RPN): ";
    for (const Lexeme& p : rpn) cout << p.symbol << ' ';
    cout << "\n\n";

    vector<StaffRow> staff = {
        {"Aarav",  1, 22},
        {"Diya",   2, 22},
        {"Rohan",  3, 28},
        {"Meera",  4, 24},
        {"Kabir",  5, 22},
        {"Ishita", 6, 31},
    };

    cout << "Rows matching the WHERE clause:\n";
    for (const StaffRow& person : staff) {
        if (runPostfix(rpn, person))
            cout << "  " << person.fullName << " (id=" << person.recordId
                 << ", age=" << person.years << ")\n";
    }

    return 0;
}
