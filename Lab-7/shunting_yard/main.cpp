// Lab 7 - SQL WHERE clause via Dijkstra's shunting-yard (infix -> RPN)
// Krritin Keshan (24BCS10122)
// the WHERE clause is tokenized, converted from infix to postfix with an
// operator stack + a precedence table (comparisons > AND > OR), then the
// postfix is evaluated once per row with a single int stack. no recursion and
// no parse tree -- the operator order in the RPN already says what to do first.

#include <iostream>
#include <string>
#include <vector>
#include <stack>
#include <cctype>

struct Token {
    std::string lexeme;
    bool isOperator;
};

static int precedence(const std::string& op) {
    if (op == ">" || op == "<" || op == ">=" || op == "<=" || op == "=") return 3;
    if (op == "AND") return 2;
    if (op == "OR")  return 1;
    return 0;
}

static std::string upper(const std::string& s) {
    std::string r;
    for (char c : s) r.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    return r;
}

static std::vector<Token> tokenize(const std::string& src) {
    std::vector<Token> toks;
    std::size_t i = 0;
    while (i < src.size()) {
        char c = src[i];
        if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::size_t j = i;
            while (j < src.size() && (std::isalnum(static_cast<unsigned char>(src[j])) || src[j] == '_')) ++j;
            std::string w = src.substr(i, j - i);
            std::string u = upper(w);
            if (u == "AND" || u == "OR") toks.push_back({u, true});
            else                         toks.push_back({w, false});
            i = j;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            std::size_t j = i;
            while (j < src.size() && std::isdigit(static_cast<unsigned char>(src[j]))) ++j;
            toks.push_back({src.substr(i, j - i), false});
            i = j;
            continue;
        }
        if ((c == '>' || c == '<') && i + 1 < src.size() && src[i + 1] == '=') {
            toks.push_back({std::string() + c + '=', true});
            i += 2;
            continue;
        }
        if (c == '>' || c == '<' || c == '=') { toks.push_back({std::string(1, c), true});  ++i; continue; }
        if (c == '(' || c == ')')              { toks.push_back({std::string(1, c), false}); ++i; continue; }
        ++i;
    }
    return toks;
}

static std::vector<Token> toPostfix(const std::vector<Token>& in) {
    std::vector<Token> out;
    std::stack<Token> ops;
    for (const Token& t : in) {
        if (t.lexeme == "(") {
            ops.push(t);
        } else if (t.lexeme == ")") {
            while (!ops.empty() && ops.top().lexeme != "(") { out.push_back(ops.top()); ops.pop(); }
            if (!ops.empty()) ops.pop();
        } else if (t.isOperator) {
            while (!ops.empty() && ops.top().lexeme != "(" &&
                   precedence(ops.top().lexeme) >= precedence(t.lexeme)) {
                out.push_back(ops.top());
                ops.pop();
            }
            ops.push(t);
        } else {
            out.push_back(t);
        }
    }
    while (!ops.empty()) { out.push_back(ops.top()); ops.pop(); }
    return out;
}

struct Student {
    int id;
    std::string name;
    int age;
    int marks;
};

static int columnValue(const std::string& c, const Student& s) {
    if (c == "id")    return s.id;
    if (c == "age")   return s.age;
    if (c == "marks") return s.marks;
    return 0;
}

static bool allDigits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

static bool evalPostfix(const std::vector<Token>& rpn, const Student& row) {
    std::stack<int> st;
    for (const Token& t : rpn) {
        if (!t.isOperator) {
            st.push(allDigits(t.lexeme) ? std::stoi(t.lexeme) : columnValue(t.lexeme, row));
            continue;
        }
        int b = st.top(); st.pop();
        int a = st.top(); st.pop();
        const std::string& o = t.lexeme;
        if      (o == ">")   st.push(a >  b);
        else if (o == "<")   st.push(a <  b);
        else if (o == ">=")  st.push(a >= b);
        else if (o == "<=")  st.push(a <= b);
        else if (o == "=")   st.push(a == b);
        else if (o == "AND") st.push(a && b);
        else if (o == "OR")  st.push(a || b);
    }
    return st.top() != 0;
}

int main() {
    std::string clause = "marks >= 80 AND (age < 20 OR id = 5)";
    std::cout << "Infix WHERE : " << clause << "\n";

    auto toks = tokenize(clause);
    auto rpn  = toPostfix(toks);

    std::cout << "Postfix     : ";
    for (const Token& t : rpn) std::cout << t.lexeme << ' ';
    std::cout << "\n\n";

    std::vector<Student> students = {
        {1, "Priya",  19, 88},
        {2, "Rohan",  22, 67},
        {3, "Sneha",  20, 91},
        {4, "Arjun",  23, 74},
        {5, "Meera",  21, 95},
        {6, "Karan",  18, 59},
    };

    std::cout << "Matching rows:\n";
    for (const Student& s : students) {
        if (evalPostfix(rpn, s))
            std::cout << "  " << s.name << " (id=" << s.id << ", age=" << s.age
                      << ", marks=" << s.marks << ")\n";
    }
    return 0;
}