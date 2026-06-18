// Lab 7 - 24BCS10404 - Rajveer Bishnoi
// Shunting-Yard (Dijkstra): converts a SQL WHERE clause from infix to
// postfix (RPN), then evaluates postfix against an in-memory row set.

#include <iostream>
#include <string>
#include <vector>
#include <stack>
#include <cctype>

using namespace std;

struct Lexeme {
    string symbol;
    bool isOp;
};

// Higher = binds tighter: comparisons(3) > AND(2) > OR(1)
int rankOf(const string& sym) {
    if (sym == ">" || sym == "<" || sym == ">=" || sym == "<=" || sym == "=")
        return 3;
    if (sym == "AND") return 2;
    if (sym == "OR")  return 1;
    return 0;
}

bool looksLikeOperator(const string& s) { return rankOf(s) > 0; }

vector<Lexeme> splitIntoLexemes(const string& src) {
    vector<Lexeme> pieces;
    int cursor = 0;
    while (cursor < (int)src.size()) {
        if (isspace(src[cursor])) { ++cursor; continue; }

        if (isalpha(src[cursor])) {
            string word;
            while (cursor < (int)src.size() &&
                   (isalnum(src[cursor]) || src[cursor] == '_'))
                word += src[cursor++];
            string caps;
            for (char ch : word) caps += toupper(ch);
            if (caps == "AND" || caps == "OR")
                pieces.push_back({caps, true});
            else
                pieces.push_back({word, false});
        } else if (isdigit(src[cursor])) {
            string digits;
            while (cursor < (int)src.size() && isdigit(src[cursor]))
                digits += src[cursor++];
            pieces.push_back({digits, false});
        } else if ((src[cursor] == '>' || src[cursor] == '<') &&
                   cursor + 1 < (int)src.size() && src[cursor + 1] == '=') {
            pieces.push_back({string() + src[cursor] + '=', true});
            cursor += 2;
        } else if (src[cursor] == '>' || src[cursor] == '<' || src[cursor] == '=') {
            pieces.push_back({string(1, src[cursor]), true});
            ++cursor;
        } else if (src[cursor] == '(') {
            pieces.push_back({"(", false}); ++cursor;
        } else if (src[cursor] == ')') {
            pieces.push_back({")", false}); ++cursor;
        } else {
            ++cursor;
        }
    }
    return pieces;
}

vector<Lexeme> toReversePolish(const vector<Lexeme>& pieces) {
    vector<Lexeme> rpnQueue;
    stack<Lexeme>  opStack;

    for (const Lexeme& piece : pieces) {
        if (piece.symbol == "(") {
            opStack.push(piece);
        } else if (piece.symbol == ")") {
            while (!opStack.empty() && opStack.top().symbol != "(") {
                rpnQueue.push_back(opStack.top()); opStack.pop();
            }
            if (!opStack.empty()) opStack.pop();
        } else if (piece.isOp) {
            while (!opStack.empty() &&
                   opStack.top().symbol != "(" &&
                   rankOf(opStack.top().symbol) >= rankOf(piece.symbol)) {
                rpnQueue.push_back(opStack.top()); opStack.pop();
            }
            opStack.push(piece);
        } else {
            rpnQueue.push_back(piece);
        }
    }
    while (!opStack.empty()) {
        rpnQueue.push_back(opStack.top()); opStack.pop();
    }
    return rpnQueue;
}

struct StaffRow { string fullName; int recordId; int years; };

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
    stack<int> work;
    for (const Lexeme& piece : rpn) {
        if (!piece.isOp) {
            work.push(isNumeric(piece.symbol) ? stoi(piece.symbol)
                                              : fieldValue(piece.symbol, row));
            continue;
        }
        int rhs = work.top(); work.pop();
        int lhs = work.top(); work.pop();
        const string& sym = piece.symbol;
        if (sym == ">")        work.push(lhs > rhs);
        else if (sym == "<")   work.push(lhs < rhs);
        else if (sym == ">=")  work.push(lhs >= rhs);
        else if (sym == "<=")  work.push(lhs <= rhs);
        else if (sym == "=")   work.push(lhs == rhs);
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
        {"Rajveer", 1, 22},
        {"Ananya",  2, 22},
        {"Rohan",   3, 28},
        {"Meera",   4, 24},
        {"Kabir",   5, 22},
        {"Ishita",  6, 31},
    };

    cout << "Rows matching the WHERE clause:\n";
    for (const StaffRow& person : staff) {
        if (runPostfix(rpn, person))
            cout << "  " << person.fullName << " (id=" << person.recordId
                 << ", age=" << person.years << ")\n";
    }

    return 0;
}
