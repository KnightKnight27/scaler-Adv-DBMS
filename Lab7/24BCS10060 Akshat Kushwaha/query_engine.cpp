// Lab 7 - Query Parsing: Shunting-Yard + a tiny SQL SELECT runner
// Akshat Kushwaha | 24BCS10060
//
// Two pieces that fit together:
//   Part 1: the shunting-yard algorithm turns an infix expression like
//           "age > 20 AND gpa >= 8" into postfix (RPN) so a stack can evaluate
//           it left to right, respecting precedence and parentheses.
//   Part 2: a very small SQL engine that parses "SELECT cols FROM t WHERE ...
//           ORDER BY c LIMIT n" and runs it over an in-memory vector of rows,
//           using Part 1 to test the WHERE clause on each row.
//
// Build: g++ -std=c++17 -Wall -Wextra query_engine.cpp -o query_engine
// Run:   ./query_engine

#include <algorithm>
#include <iostream>
#include <sstream>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// Part 1: tokenize + shunting-yard + RPN evaluation
// ---------------------------------------------------------------------------

// Higher number binds tighter. Comparisons happen before AND, AND before OR.
int precedence(const std::string& op) {
    if (op == "OR")  return 1;
    if (op == "AND") return 2;
    if (op == ">" || op == "<" || op == ">=" || op == "<=" ||
        op == "==" || op == "!=") return 3;
    return 0;
}
bool is_operator(const std::string& t) { return precedence(t) > 0; }

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// Break an expression string into tokens (identifiers, numbers, operators, parens).
std::vector<std::string> tokenize(const std::string& expr) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < expr.size()) {
        char c = expr[i];
        if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }

        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            size_t j = i;
            while (j < expr.size() &&
                   (std::isalnum(static_cast<unsigned char>(expr[j])) || expr[j] == '_'))
                ++j;
            std::string word = expr.substr(i, j - i);
            std::string up = upper(word);
            out.push_back((up == "AND" || up == "OR") ? up : word);
            i = j;
        } else if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            size_t j = i;
            while (j < expr.size() &&
                   (std::isdigit(static_cast<unsigned char>(expr[j])) || expr[j] == '.'))
                ++j;
            out.push_back(expr.substr(i, j - i));
            i = j;
        } else if (c == '(' || c == ')') {
            out.push_back(std::string(1, c));
            ++i;
        } else {
            // operators, possibly two characters (>=, <=, ==, !=)
            if (i + 1 < expr.size() && expr[i + 1] == '=' &&
                (c == '>' || c == '<' || c == '=' || c == '!')) {
                out.push_back(std::string() + c + '=');
                i += 2;
            } else {
                out.push_back(std::string(1, c));
                ++i;
            }
        }
    }
    return out;
}

// Shunting-yard: infix tokens -> postfix (RPN) tokens.
std::vector<std::string> to_rpn(const std::vector<std::string>& tokens) {
    std::vector<std::string> output;
    std::stack<std::string>  ops;
    for (const std::string& tok : tokens) {
        if (tok == "(") {
            ops.push(tok);
        } else if (tok == ")") {
            while (!ops.empty() && ops.top() != "(") { output.push_back(ops.top()); ops.pop(); }
            if (!ops.empty()) ops.pop();                 // pop the "("
        } else if (is_operator(tok)) {
            while (!ops.empty() && ops.top() != "(" &&
                   precedence(ops.top()) >= precedence(tok)) {
                output.push_back(ops.top()); ops.pop();
            }
            ops.push(tok);
        } else {
            output.push_back(tok);                       // operand (number or column)
        }
    }
    while (!ops.empty()) { output.push_back(ops.top()); ops.pop(); }
    return output;
}

using Row = std::unordered_map<std::string, double>;

bool looks_numeric(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) if (!std::isdigit(static_cast<unsigned char>(c)) && c != '.') return false;
    return true;
}

// Evaluate the RPN for one row. Operands resolve to a number (a literal, or the
// row's value for that column). The final value on the stack is the truth value.
double eval_rpn(const std::vector<std::string>& rpn, const Row& row) {
    std::stack<double> st;
    for (const std::string& tok : rpn) {
        if (!is_operator(tok)) {
            if (looks_numeric(tok)) st.push(std::stod(tok));
            else {
                auto it = row.find(tok);
                st.push(it == row.end() ? 0.0 : it->second);
            }
            continue;
        }
        double b = st.top(); st.pop();
        double a = st.top(); st.pop();
        if      (tok == ">")  st.push(a >  b);
        else if (tok == "<")  st.push(a <  b);
        else if (tok == ">=") st.push(a >= b);
        else if (tok == "<=") st.push(a <= b);
        else if (tok == "==") st.push(a == b);
        else if (tok == "!=") st.push(a != b);
        else if (tok == "AND") st.push((a != 0) && (b != 0));
        else if (tok == "OR")  st.push((a != 0) || (b != 0));
    }
    return st.empty() ? 1.0 : st.top();
}

// ---------------------------------------------------------------------------
// Part 2: a tiny SELECT parser + executor over vector<Row>
// ---------------------------------------------------------------------------

struct Query {
    std::vector<std::string> columns;   // empty means SELECT *
    std::string              where;     // raw WHERE text ("" = no filter)
    std::string              order_by;  // "" = no ordering
    bool                     asc = true;
    int                      limit = -1;
};

Query parse_select(const std::string& sql) {
    Query q;
    std::istringstream ss(sql);
    std::string w;
    ss >> w;                                            // SELECT
    while (ss >> w && upper(w) != "FROM") {
        if (!w.empty() && w.back() == ',') w.pop_back();
        if (w == "*") q.columns.clear();
        else          q.columns.push_back(w);
    }
    ss >> w;                                            // table name (ignored, data is in memory)
    std::string clause;
    while (ss >> w) {
        std::string up = upper(w);
        if (up == "WHERE") {
            std::string token;
            while (ss >> token) {
                std::string ut = upper(token);
                if (ut == "ORDER" || ut == "LIMIT") { w = token; up = ut; break; }
                clause += (clause.empty() ? "" : " ") + token;
            }
            q.where = clause;
            if (up != "ORDER" && up != "LIMIT") continue;
        }
        if (up == "ORDER") {
            ss >> w;                                    // BY
            ss >> q.order_by;
            std::string dir;
            if (ss >> dir && upper(dir) == "DESC") q.asc = false;
        } else if (up == "LIMIT") {
            ss >> q.limit;
        }
    }
    return q;
}

std::vector<Row> run(const Query& q, const std::vector<Row>& table) {
    std::vector<std::string> rpn;
    if (!q.where.empty()) rpn = to_rpn(tokenize(q.where));

    std::vector<Row> result;
    for (const Row& row : table) {
        if (!rpn.empty() && eval_rpn(rpn, row) == 0.0) continue;     // WHERE filter
        if (q.columns.empty()) {
            result.push_back(row);
        } else {
            Row projected;
            for (const std::string& col : q.columns)
                if (row.count(col)) projected[col] = row.at(col);
            result.push_back(projected);
        }
    }
    if (!q.order_by.empty()) {
        std::sort(result.begin(), result.end(), [&](const Row& a, const Row& b) {
            double va = a.count(q.order_by) ? a.at(q.order_by) : 0.0;
            double vb = b.count(q.order_by) ? b.at(q.order_by) : 0.0;
            return q.asc ? va < vb : va > vb;
        });
    }
    if (q.limit >= 0 && static_cast<int>(result.size()) > q.limit)
        result.resize(q.limit);
    return result;
}

void print_rows(const std::vector<Row>& rows) {
    if (rows.empty()) { std::cout << "  (no rows)\n"; return; }
    for (const Row& r : rows) {
        std::cout << "  ";
        for (const auto& [k, v] : r) std::cout << k << "=" << v << "  ";
        std::cout << "\n";
    }
}

int main() {
    std::cout << "Query engine | Akshat Kushwaha | 24BCS10060\n\n";

    // ---- Part 1 demo: show the infix -> RPN conversion ----
    std::string expr = "age > 20 AND (gpa >= 8 OR id == 3)";
    auto rpn = to_rpn(tokenize(expr));
    std::cout << "infix : " << expr << "\nRPN   : ";
    for (const auto& t : rpn) std::cout << t << " ";
    std::cout << "\n\n";

    // ---- Part 2 demo: run SELECT queries over in-memory rows ----
    std::vector<Row> students = {
        {{"id", 1}, {"age", 22}, {"gpa", 9}},
        {{"id", 2}, {"age", 19}, {"gpa", 7}},
        {{"id", 3}, {"age", 25}, {"gpa", 6}},
        {{"id", 4}, {"age", 21}, {"gpa", 8}},
        {{"id", 5}, {"age", 23}, {"gpa", 9}},
    };

    std::vector<std::string> queries = {
        "SELECT id, gpa FROM students WHERE age > 20 AND gpa >= 8 ORDER BY gpa DESC",
        "SELECT * FROM students WHERE gpa < 8 OR id == 5",
    };

    for (const std::string& sql : queries) {
        std::cout << "SQL: " << sql << "\n";
        Query q = parse_select(sql);
        print_rows(run(q, students));
        std::cout << "\n";
    }
    return 0;
}
