// Lab 7 — SQL WHERE evaluation via the shunting-yard algorithm.
//
// Name : Shaurya Verma
// Roll : 24BCS10151
//
// We take the WHERE clause of
//
//     SELECT name FROM students WHERE marks >= 80 AND (age < 20 OR id = 5)
//
// tokenise it, convert the infix token stream to Reverse Polish Notation
// (RPN) with Dijkstra's shunting-yard, and then evaluate that RPN once
// per row with a small boolean/int stack. Operator precedence lives in a
// single lookup table — nowhere else.

#include <iostream>
#include <string>
#include <vector>
#include <stack>
#include <sstream>
#include <unordered_map>

namespace {

struct Row {
    int id;
    std::string name;
    int age;
    int marks;
};

// A token is either an operator (AND/OR/comparison), a parenthesis, a
// column name, or an integer literal. We keep the kind explicit so the
// evaluator never has to re-parse a string.
enum class Kind { Column, Number, Op, LParen, RParen };

struct Token {
    Kind kind;
    std::string text;   // operator symbol or column name
    int value = 0;      // populated when kind == Number
};

// Lower number == binds looser. Comparisons bind tightest, then AND,
// then OR — exactly the SQL rule. This table IS the precedence logic.
int precedence(const std::string& op) {
    if (op == "OR")  return 1;
    if (op == "AND") return 2;
    return 3;            // =, <, >, <=, >=
}

bool is_operator(const std::string& t) {
    return t == "AND" || t == "OR" || t == "=" || t == "<" ||
           t == ">" || t == "<=" || t == ">=";
}

// Split the clause into tokens. We rely on the clause being space
// separated except around parentheses, which we peel off explicitly.
std::vector<Token> tokenise(const std::string& clause) {
    std::vector<Token> tokens;
    std::istringstream in(clause);
    std::string word;

    while (in >> word) {
        // A word like "(age" or "5)" needs its parens stripped.
        size_t i = 0;
        while (i < word.size()) {
            char c = word[i];
            if (c == '(') {
                tokens.push_back({Kind::LParen, "(", 0});
                ++i;
            } else if (c == ')') {
                tokens.push_back({Kind::RParen, ")", 0});
                ++i;
            } else {
                size_t start = i;
                while (i < word.size() && word[i] != '(' && word[i] != ')')
                    ++i;
                std::string piece = word.substr(start, i - start);
                if (is_operator(piece)) {
                    tokens.push_back({Kind::Op, piece, 0});
                } else if (!piece.empty() &&
                           (std::isdigit(static_cast<unsigned char>(piece[0])))) {
                    tokens.push_back({Kind::Number, piece, std::stoi(piece)});
                } else {
                    tokens.push_back({Kind::Column, piece, 0});
                }
            }
        }
    }
    return tokens;
}

// Classic shunting-yard: operands flow straight to output, operators
// wait on a stack until something of equal-or-higher precedence forces
// them out. Parentheses bracket sub-expressions.
std::vector<Token> to_rpn(const std::vector<Token>& tokens) {
    std::vector<Token> output;
    std::stack<Token> ops;

    for (const Token& t : tokens) {
        switch (t.kind) {
            case Kind::Column:
            case Kind::Number:
                output.push_back(t);
                break;
            case Kind::Op:
                while (!ops.empty() && ops.top().kind == Kind::Op &&
                       precedence(ops.top().text) >= precedence(t.text)) {
                    output.push_back(ops.top());
                    ops.pop();
                }
                ops.push(t);
                break;
            case Kind::LParen:
                ops.push(t);
                break;
            case Kind::RParen:
                while (!ops.empty() && ops.top().kind != Kind::LParen) {
                    output.push_back(ops.top());
                    ops.pop();
                }
                if (!ops.empty()) ops.pop();   // discard the '('
                break;
        }
    }
    while (!ops.empty()) {
        output.push_back(ops.top());
        ops.pop();
    }
    return output;
}

// Look up a column's value for a given row.
int column_value(const std::string& col, const Row& r) {
    if (col == "id")    return r.id;
    if (col == "age")   return r.age;
    if (col == "marks") return r.marks;
    return 0;
}

// Evaluate the RPN against one row. The stack holds ints; comparison
// operators push 0/1, and AND/OR treat anything non-zero as true.
bool eval_rpn(const std::vector<Token>& rpn, const Row& r) {
    std::stack<int> st;

    for (const Token& t : rpn) {
        if (t.kind == Kind::Number) {
            st.push(t.value);
        } else if (t.kind == Kind::Column) {
            st.push(column_value(t.text, r));
        } else {                       // operator
            int rhs = st.top(); st.pop();
            int lhs = st.top(); st.pop();
            const std::string& op = t.text;
            int res = 0;
            if (op == "=")       res = (lhs == rhs);
            else if (op == "<")  res = (lhs <  rhs);
            else if (op == ">")  res = (lhs >  rhs);
            else if (op == "<=") res = (lhs <= rhs);
            else if (op == ">=") res = (lhs >= rhs);
            else if (op == "AND") res = (lhs && rhs);
            else if (op == "OR")  res = (lhs || rhs);
            st.push(res);
        }
    }
    return st.top() != 0;
}

std::string join_rpn(const std::vector<Token>& rpn) {
    std::string out;
    for (size_t i = 0; i < rpn.size(); ++i) {
        if (i) out += ' ';
        out += rpn[i].text;
    }
    return out;
}

}  // namespace

int main() {
    const std::vector<Row> students = {
        {1, "Priya", 19, 88},
        {2, "Rohan", 22, 67},
        {3, "Sneha", 20, 91},
        {4, "Arjun", 23, 74},
        {5, "Meera", 21, 95},
        {6, "Karan", 18, 59},
    };

    const std::string where = "marks >= 80 AND ( age < 20 OR id = 5 )";

    std::vector<Token> tokens = tokenise(where);
    std::vector<Token> rpn = to_rpn(tokens);

    std::cout << "Lab 7 - shunting-yard (Shaurya Verma, 24BCS10151)\n\n";
    std::cout << "Infix WHERE : marks >= 80 AND (age < 20 OR id = 5)\n";
    std::cout << "Postfix RPN : " << join_rpn(rpn) << "\n\n";

    std::cout << "Matching rows (SELECT name ...):\n";
    for (const Row& r : students) {
        if (eval_rpn(rpn, r)) {
            std::cout << "  " << r.name
                      << "  (id=" << r.id
                      << ", age=" << r.age
                      << ", marks=" << r.marks << ")\n";
        }
    }
    return 0;
}
