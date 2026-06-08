#include <iostream>
#include <string>
#include <vector>
#include <stack>
#include <cctype>

using namespace std;

struct Tok {
    string s;
    bool   op;
};

static int prec(const string& o) {
    if (o == ">" || o == "<" || o == ">=" || o == "<=" || o == "=") return 3;
    if (o == "AND") return 2;
    if (o == "OR")  return 1;
    return 0;
}

static string upcase(const string& s) {
    string r;
    for (char c : s) r.push_back((char)toupper((unsigned char)c));
    return r;
}

static vector<Tok> tokenize(const string& src) {
    vector<Tok> out;
    size_t i = 0;
    while (i < src.size()) {
        char c = src[i];
        if (isspace((unsigned char)c)) { ++i; continue; }

        if (isalpha((unsigned char)c) || c == '_') {
            size_t j = i;
            while (j < src.size() && (isalnum((unsigned char)src[j]) || src[j] == '_')) ++j;
            string word = src.substr(i, j - i);
            string up = upcase(word);
            if (up == "AND" || up == "OR") out.push_back({up, true});
            else                           out.push_back({word, false});
            i = j;
            continue;
        }

        if (isdigit((unsigned char)c)) {
            size_t j = i;
            while (j < src.size() && isdigit((unsigned char)src[j])) ++j;
            out.push_back({src.substr(i, j - i), false});
            i = j;
            continue;
        }

        if ((c == '>' || c == '<') && i + 1 < src.size() && src[i + 1] == '=') {
            out.push_back({string() + c + '=', true});
            i += 2;
            continue;
        }

        if (c == '>' || c == '<' || c == '=') { out.push_back({string(1, c), true});  ++i; continue; }
        if (c == '(' || c == ')')              { out.push_back({string(1, c), false}); ++i; continue; }
        ++i;
    }
    return out;
}

// Shunting-yard: infix tokens -> postfix tokens.
// Operators are all left-associative, so we pop while the operator on top of
// the stack has precedence >= the incoming operator.
static vector<Tok> shuntingYard(const vector<Tok>& in) {
    vector<Tok>  out;
    stack<Tok>   ops;

    for (const Tok& t : in) {
        if (t.s == "(") {
            ops.push(t);
        } else if (t.s == ")") {
            while (!ops.empty() && ops.top().s != "(") {
                out.push_back(ops.top());
                ops.pop();
            }
            if (!ops.empty()) ops.pop();
        } else if (t.op) {
            while (!ops.empty() && ops.top().s != "(" &&
                   prec(ops.top().s) >= prec(t.s)) {
                out.push_back(ops.top());
                ops.pop();
            }
            ops.push(t);
        } else {
            out.push_back(t);
        }
    }
    while (!ops.empty()) {
        out.push_back(ops.top());
        ops.pop();
    }
    return out;
}

struct Student {
    string name;
    int    id;
    int    marks;
};

static int columnValue(const string& col, const Student& s) {
    if (col == "id")    return s.id;
    if (col == "marks") return s.marks;
    return 0;
}

static bool isNumber(const string& s) {
    for (char c : s) if (!isdigit((unsigned char)c)) return false;
    return !s.empty();
}

static bool evalPostfix(const vector<Tok>& rpn, const Student& row) {
    stack<int> st;
    for (const Tok& t : rpn) {
        if (!t.op) {
            st.push(isNumber(t.s) ? stoi(t.s) : columnValue(t.s, row));
            continue;
        }
        int b = st.top(); st.pop();
        int a = st.top(); st.pop();
        const string& o = t.s;
        if      (o == ">")   st.push(a >  b);
        else if (o == "<")   st.push(a <  b);
        else if (o == ">=")  st.push(a >= b);
        else if (o == "<=")  st.push(a <= b);
        else if (o == "=")   st.push(a == b);
        else if (o == "AND") st.push(a && b);
        else if (o == "OR")  st.push(a || b);
    }
    return st.top();
}

int main() {
    string clause = "marks >= 80 AND (id < 3 OR id > 5)";

    cout << "Infix WHERE : " << clause << "\n";

    auto toks = tokenize(clause);
    auto rpn  = shuntingYard(toks);

    cout << "Postfix     : ";
    for (const Tok& t : rpn) cout << t.s << ' ';
    cout << "\n\n";

    vector<Student> students = {
        {"Aarav", 1, 78},
        {"Diya",  2, 91},
        {"Kabir", 3, 55},
        {"Meera", 4, 88},
        {"Rohan", 5, 42},
        {"Sneha", 6, 95},
    };

    cout << "Rows matching the WHERE clause:\n";
    for (const Student& s : students) {
        if (evalPostfix(rpn, s))
            cout << "  " << s.name << " (id=" << s.id << ", marks=" << s.marks << ")\n";
    }
    return 0;
}
