// Lab Session 5: Shunting-Yard Algorithm + Minimal SQL SELECT Parser
//
// 1. Dijkstra's Shunting-Yard: infix arithmetic/boolean expressions -> RPN,
//    evaluated with a stack (the mechanism behind SQL WHERE evaluation).
// 2. A minimal SQL parser that handles SELECT ... FROM ... WHERE ... ORDER BY
//    ... LIMIT and executes it against an in-memory vector<Row>.
//
// Build:  g++ -std=c++17 -Wall -Wextra -O2 -o sql_parser sql_parser.cpp
// Run:    ./sql_parser

#include <iostream>
#include <sstream>
#include <stack>
#include <vector>
#include <string>
#include <unordered_map>
#include <stdexcept>
#include <cctype>
#include <cmath>
#include <variant>
#include <algorithm>

// ── Operator metadata ──────────────────────────────────────────
struct OpInfo { int precedence; bool right_assoc; };

const std::unordered_map<std::string, OpInfo> OPS = {
    {"||", {1, false}},   // OR  (logical)
    {"&&", {2, false}},   // AND
    {"=",  {3, false}},   // equality
    {"!=", {3, false}},
    {"<",  {4, false}},
    {">",  {4, false}},
    {"<=", {4, false}},
    {">=", {4, false}},
    {"+",  {5, false}},
    {"-",  {5, false}},
    {"*",  {6, false}},
    {"/",  {6, false}},
    {"^",  {7, true }},   // exponentiation (right-associative)
};

// Tokenize: numbers, identifiers, operators, parens.
std::vector<std::string> tokenize(const std::string& expr) {
    std::vector<std::string> tokens;
    int i = 0, n = (int)expr.size();
    while (i < n) {
        if (std::isspace((unsigned char)expr[i])) { i++; continue; }
        if (std::isdigit((unsigned char)expr[i]) ||
            (expr[i] == '.' && i+1 < n && std::isdigit((unsigned char)expr[i+1]))) {
            int j = i;
            while (j < n && (std::isdigit((unsigned char)expr[j]) || expr[j] == '.')) j++;
            tokens.push_back(expr.substr(i, j - i));
            i = j;
        } else if (std::isalpha((unsigned char)expr[i]) || expr[i] == '_') {
            int j = i;
            while (j < n && (std::isalnum((unsigned char)expr[j]) || expr[j] == '_')) j++;
            tokens.push_back(expr.substr(i, j - i));
            i = j;
        } else if (expr[i] == '(' || expr[i] == ')') {
            tokens.push_back(std::string(1, expr[i++]));
        } else {
            if (i+1 < n) {                                   // two-char operators
                std::string two = expr.substr(i, 2);
                if (OPS.count(two)) { tokens.push_back(two); i += 2; continue; }
            }
            tokens.push_back(std::string(1, expr[i++]));
        }
    }
    return tokens;
}

// Shunting-Yard: infix tokens -> postfix (RPN).
std::vector<std::string> to_rpn(const std::vector<std::string>& tokens) {
    std::vector<std::string> output;
    std::stack<std::string>  ops;

    for (const auto& tok : tokens) {
        if (tok == "(") {
            ops.push(tok);
        } else if (tok == ")") {
            while (!ops.empty() && ops.top() != "(") { output.push_back(ops.top()); ops.pop(); }
            if (ops.empty()) throw std::runtime_error("Mismatched parentheses");
            ops.pop();                                       // discard '('
        } else if (OPS.count(tok)) {
            const auto& o1 = OPS.at(tok);
            while (!ops.empty() && OPS.count(ops.top())) {
                const auto& o2 = OPS.at(ops.top());
                if (o2.precedence > o1.precedence ||
                   (o2.precedence == o1.precedence && !o1.right_assoc)) {
                    output.push_back(ops.top()); ops.pop();
                } else break;
            }
            ops.push(tok);
        } else {
            output.push_back(tok);                           // number or identifier
        }
    }
    while (!ops.empty()) {
        if (ops.top() == "(") throw std::runtime_error("Mismatched parentheses");
        output.push_back(ops.top()); ops.pop();
    }
    return output;
}

// Evaluate RPN with a variable map (values treated as doubles).
double eval_rpn(const std::vector<std::string>& rpn,
                const std::unordered_map<std::string, double>& vars) {
    std::stack<double> stk;
    for (const auto& tok : rpn) {
        if (OPS.count(tok)) {
            double b = stk.top(); stk.pop();
            double a = stk.top(); stk.pop();
            if      (tok == "+")  stk.push(a + b);
            else if (tok == "-")  stk.push(a - b);
            else if (tok == "*")  stk.push(a * b);
            else if (tok == "/")  stk.push(a / b);
            else if (tok == "^")  stk.push(std::pow(a, b));
            else if (tok == "<")  stk.push(a < b  ? 1.0 : 0.0);
            else if (tok == ">")  stk.push(a > b  ? 1.0 : 0.0);
            else if (tok == "<=") stk.push(a <= b ? 1.0 : 0.0);
            else if (tok == ">=") stk.push(a >= b ? 1.0 : 0.0);
            else if (tok == "=")  stk.push(a == b ? 1.0 : 0.0);
            else if (tok == "!=") stk.push(a != b ? 1.0 : 0.0);
            else if (tok == "&&") stk.push((a && b) ? 1.0 : 0.0);
            else if (tok == "||") stk.push((a || b) ? 1.0 : 0.0);
        } else {
            try { stk.push(std::stod(tok)); }
            catch (...) {
                auto it = vars.find(tok);
                if (it == vars.end()) throw std::runtime_error("Unknown variable: " + tok);
                stk.push(it->second);
            }
        }
    }
    return stk.top();
}

void shunting_demo() {
    std::string expr = "age * 2 + salary / 1000 > 100";
    auto tokens = tokenize(expr);
    auto rpn    = to_rpn(tokens);

    std::cout << "Expression : " << expr << "\n";
    std::cout << "RPN        : ";
    for (auto& t : rpn) std::cout << t << " ";
    std::cout << "\n";

    std::unordered_map<std::string, double> vars = {{"age", 30}, {"salary", 50000}};
    double result = eval_rpn(rpn, vars);
    std::cout << "Result     : " << (result ? "true" : "false") << "\n\n";
}

// ── Part 2: Minimal SQL SELECT parser over vector<Row> ─────────
using Value = std::variant<double, std::string>;

struct Row { std::unordered_map<std::string, Value> cols; };

double row_val(const Row& row, const std::string& col) {
    auto it = row.cols.find(col);
    if (it == row.cols.end()) return 0.0;
    if (auto* d = std::get_if<double>(&it->second)) return *d;
    if (auto* s = std::get_if<std::string>(&it->second)) {
        try { return std::stod(*s); } catch (...) {}
    }
    return 0.0;
}

struct SelectQuery {
    std::vector<std::string> columns;     // empty = SELECT *
    std::string              from;
    std::string              where_raw;
    std::string              order_by;
    bool                     order_asc = true;
    int                      limit = -1;
};

std::string to_upper(std::string s) {
    for (auto& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

SelectQuery parse_select(const std::string& sql) {
    SelectQuery q;
    std::istringstream ss(sql);
    std::string word;
    ss >> word;                                              // SELECT

    while (ss >> word && to_upper(word) != "FROM") {         // column list
        if (!word.empty() && word.back() == ',') word.pop_back();
        if (word == "*") q.columns.clear();
        else             q.columns.push_back(word);
    }

    ss >> q.from;                                            // table name

    while (ss >> word) {
        std::string kw = to_upper(word);
        if (kw == "WHERE") {
            std::string clause, w2;
            while (ss >> w2) {
                if (to_upper(w2) == "ORDER" || to_upper(w2) == "LIMIT") {
                    word = w2; goto next_clause;
                }
                clause += (clause.empty() ? "" : " ") + w2;
            }
            q.where_raw = clause;
            break;
        next_clause:;
            q.where_raw = clause;
            kw = to_upper(word);
        }
        if (kw == "ORDER") {
            ss >> word;                                      // BY
            ss >> q.order_by;
            std::string dir;
            if (ss >> dir && to_upper(dir) == "DESC") q.order_asc = false;
        }
        if (kw == "LIMIT") ss >> q.limit;
    }
    return q;
}

std::vector<Row> execute(const SelectQuery& q, const std::vector<Row>& data) {
    std::vector<std::string> rpn;
    if (!q.where_raw.empty()) rpn = to_rpn(tokenize(q.where_raw));

    std::vector<Row> result;
    for (const auto& row : data) {
        if (!rpn.empty()) {
            std::unordered_map<std::string, double> vars;
            for (auto& [k, v] : row.cols) { (void)v; vars[k] = row_val(row, k); }
            if (!eval_rpn(rpn, vars)) continue;
        }
        if (q.columns.empty()) {
            result.push_back(row);
        } else {
            Row projected;
            for (auto& col : q.columns)
                if (row.cols.count(col)) projected.cols[col] = row.cols.at(col);
            result.push_back(projected);
        }
    }

    if (!q.order_by.empty()) {
        std::sort(result.begin(), result.end(), [&](const Row& a, const Row& b) {
            double va = row_val(a, q.order_by), vb = row_val(b, q.order_by);
            return q.order_asc ? va < vb : va > vb;
        });
    }
    if (q.limit >= 0 && (int)result.size() > q.limit) result.resize(q.limit);
    return result;
}

void print_rows(const std::vector<Row>& rows) {
    for (const auto& row : rows) {
        for (const auto& [k, v] : row.cols) {
            std::cout << k << "=";
            if (auto* d = std::get_if<double>(&v))      std::cout << *d;
            if (auto* s = std::get_if<std::string>(&v)) std::cout << *s;
            std::cout << "  ";
        }
        std::cout << "\n";
    }
}

int main() {
    shunting_demo();

    std::vector<Row> students = {
        {{{ "id", 1.0 }, { "name", std::string("Alice") }, { "age", 22.0 }, { "gpa", 3.8 }}},
        {{{ "id", 2.0 }, { "name", std::string("Bob")   }, { "age", 25.0 }, { "gpa", 2.9 }}},
        {{{ "id", 3.0 }, { "name", std::string("Carol") }, { "age", 21.0 }, { "gpa", 3.5 }}},
        {{{ "id", 4.0 }, { "name", std::string("Dave")  }, { "age", 30.0 }, { "gpa", 3.1 }}},
    };

    std::vector<std::string> queries = {
        "SELECT id, name, gpa FROM students WHERE gpa > 3.0 ORDER BY gpa DESC LIMIT 3",
        "SELECT * FROM students WHERE age >= 22 && age <= 26",
    };

    for (auto& sql : queries) {
        std::cout << "SQL: " << sql << "\n";
        auto q   = parse_select(sql);
        auto res = execute(q, students);
        print_rows(res);
        std::cout << "\n";
    }
    return 0;
}
