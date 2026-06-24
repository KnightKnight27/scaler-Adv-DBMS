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
#include <functional>

// ============================================================================
// Part 1: Shunting-Yard Algorithm (Expression Evaluator)
// ============================================================================

// Operator metadata
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

// Tokenize: numbers, identifiers, operators, parens
std::vector<std::string> tokenize(const std::string& expr) {
    std::vector<std::string> tokens;
    int i = 0, n = expr.size();
    while (i < n) {
        if (std::isspace(expr[i])) { i++; continue; }
        if (std::isdigit(expr[i]) || (expr[i] == '.' && i+1 < n && std::isdigit(expr[i+1]))) {
            int j = i;
            while (j < n && (std::isdigit(expr[j]) || expr[j] == '.')) j++;
            tokens.push_back(expr.substr(i, j - i));
            i = j;
        } else if (std::isalpha(expr[i]) || expr[i] == '_') {
            int j = i;
            while (j < n && (std::isalnum(expr[j]) || expr[j] == '_')) j++;
            tokens.push_back(expr.substr(i, j - i));
            i = j;
        } else if (expr[i] == '(' || expr[i] == ')') {
            tokens.push_back(std::string(1, expr[i++]));
        } else {
            // Two-char operators
            if (i+1 < n) {
                std::string two = expr.substr(i, 2);
                if (OPS.count(two)) { tokens.push_back(two); i += 2; continue; }
            }
            tokens.push_back(std::string(1, expr[i++]));
        }
    }
    return tokens;
}

// Shunting-Yard: infix tokens -> postfix (RPN) tokens
std::vector<std::string> to_rpn(const std::vector<std::string>& tokens) {
    std::vector<std::string> output;
    std::stack<std::string>  ops;

    for (const auto& tok : tokens) {
        if (tok == "(") {
            ops.push(tok);
        } else if (tok == ")") {
            while (!ops.empty() && ops.top() != "(") {
                output.push_back(ops.top()); ops.pop();
            }
            if (ops.empty()) throw std::runtime_error("Mismatched parentheses");
            ops.pop(); // discard '('
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
            output.push_back(tok); // number or identifier
        }
    }
    while (!ops.empty()) {
        if (ops.top() == "(") throw std::runtime_error("Mismatched parentheses");
        output.push_back(ops.top()); ops.pop();
    }
    return output;
}

// Evaluate RPN with a variable map (all values treated as doubles for simplicity)
double eval_rpn(const std::vector<std::string>& rpn,
                const std::unordered_map<std::string, double>& vars) {
    std::stack<double> stk;
    for (const auto& tok : rpn) {
        if (OPS.count(tok)) {
            if (stk.size() < 2) {
                throw std::runtime_error("Invalid expression (insufficient operands for " + tok + ")");
            }
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
            else if (tok == "&&") stk.push((a != 0.0 && b != 0.0) ? 1.0 : 0.0);
            else if (tok == "||") stk.push((a != 0.0 || b != 0.0) ? 1.0 : 0.0);
        } else {
            try { stk.push(std::stod(tok)); }
            catch (...) {
                auto it = vars.find(tok);
                if (it == vars.end())
                    throw std::runtime_error("Unknown variable: " + tok);
                stk.push(it->second);
            }
        }
    }
    if (stk.empty()) return 0.0;
    return stk.top();
}

// Demo
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

// ============================================================================
// Part 2: Minimal SQL SELECT Parser over vector<Row>
// ============================================================================

using Value = std::variant<double, std::string>;

struct Row {
    std::unordered_map<std::string, Value> cols;
};

// Helper: get numeric value from a Row column (for expression evaluation)
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
    std::vector<std::string>  columns;   // empty = SELECT *
    std::string               from;      // table name
    std::string               where_raw; // raw WHERE clause string
    std::string               order_by;  // column name
    bool                      order_asc = true;
    int                       limit = -1;
};

// Minimal tokenizer for SQL keywords (case-insensitive)
std::string to_upper(std::string s) {
    for (auto& c : s) c = std::toupper(c);
    return s;
}

// Robust SQL select parser
SelectQuery parse_select(const std::string& sql) {
    SelectQuery q;
    std::istringstream ss(sql);
    std::vector<std::string> words;
    std::string word;
    while (ss >> word) {
        words.push_back(word);
    }

    if (words.empty() || to_upper(words[0]) != "SELECT") {
        return q;
    }

    size_t idx = 1;

    // Read column list until FROM
    while (idx < words.size() && to_upper(words[idx]) != "FROM") {
        std::string col = words[idx];
        if (!col.empty() && col.back() == ',') col.pop_back();
        if (col == "*") q.columns.clear();
        else             q.columns.push_back(col);
        idx++;
    }

    if (idx < words.size() && to_upper(words[idx]) == "FROM") {
        idx++; // skip FROM
    }
    if (idx < words.size()) {
        q.from = words[idx];
        idx++;
    }

    // Read optional clauses: WHERE, ORDER BY, LIMIT
    while (idx < words.size()) {
        std::string kw = to_upper(words[idx]);
        if (kw == "WHERE") {
            idx++;
            std::string clause;
            while (idx < words.size()) {
                std::string next_kw = to_upper(words[idx]);
                if (next_kw == "ORDER" || next_kw == "LIMIT") {
                    break;
                }
                clause += (clause.empty() ? "" : " ") + words[idx];
                idx++;
            }
            q.where_raw = clause;
        } else if (kw == "ORDER") {
            if (idx + 2 < words.size() && to_upper(words[idx+1]) == "BY") {
                q.order_by = words[idx+2];
                idx += 3;
                if (idx < words.size() && to_upper(words[idx]) == "DESC") {
                    q.order_asc = false;
                    idx++;
                } else if (idx < words.size() && to_upper(words[idx]) == "ASC") {
                    q.order_asc = true;
                    idx++;
                }
            } else {
                idx++; // incomplete order by
            }
        } else if (kw == "LIMIT") {
            if (idx + 1 < words.size()) {
                try {
                    q.limit = std::stoi(words[idx+1]);
                } catch (...) {
                    q.limit = -1;
                }
                idx += 2;
            } else {
                idx++;
            }
        } else {
            idx++; // skip unknown keyword
        }
    }
    return q;
}

std::vector<Row> execute(const SelectQuery& q, const std::vector<Row>& data) {
    std::vector<std::string> rpn;
    if (!q.where_raw.empty())
        rpn = to_rpn(tokenize(q.where_raw));

    std::vector<Row> result;

    for (const auto& row : data) {
        // Evaluate WHERE
        if (!rpn.empty()) {
            // Build variable map from row
            std::unordered_map<std::string, double> vars;
            for (auto& [k, v] : row.cols) vars[k] = row_val(row, k);
            if (!eval_rpn(rpn, vars)) continue;
        }

        // Project columns
        if (q.columns.empty()) {
            result.push_back(row);
        } else {
            Row projected;
            for (auto& col : q.columns)
                if (row.cols.count(col)) projected.cols[col] = row.cols.at(col);
            result.push_back(projected);
        }
    }

    // ORDER BY
    if (!q.order_by.empty()) {
        std::sort(result.begin(), result.end(), [&](const Row& a, const Row& b) {
            double va = row_val(a, q.order_by);
            double vb = row_val(b, q.order_by);
            return q.order_asc ? va < vb : va > vb;
        });
    }

    // LIMIT
    if (q.limit >= 0 && (int)result.size() > q.limit)
        result.resize(q.limit);

    return result;
}

void print_rows(const std::vector<Row>& rows) {
    for (const auto& row : rows) {
        for (const auto& [k, v] : row.cols) {
            std::cout << k << "=";
            if (auto* d = std::get_if<double>(&v))   std::cout << *d;
            if (auto* s = std::get_if<std::string>(&v)) std::cout << *s;
            std::cout << "  ";
        }
        std::cout << "\n";
    }
}

int main() {
    shunting_demo();

    // Pre-fetched data (simulates what a storage layer returns)
    std::vector<Row> students = {
        {{{ "id", 1.0 }, { "name", std::string("Alice") }, { "age", 22.0 }, { "gpa", 3.8 }}},
        {{{ "id", 2.0 }, { "name", std::string("Bob")   }, { "age", 25.0 }, { "gpa", 2.9 }}},
        {{{ "id", 3.0 }, { "name", std::string("Carol")  }, { "age", 21.0 }, { "gpa", 3.5 }}},
        {{{ "id", 4.0 }, { "name", std::string("Dave")   }, { "age", 30.0 }, { "gpa", 3.1 }}},
    };

    // Test queries
    struct { std::string sql; } queries[] = {
        { "SELECT id, name, gpa FROM students WHERE gpa > 3.0 ORDER BY gpa DESC LIMIT 3" },
        { "SELECT * FROM students WHERE age >= 22 && age <= 26" },
    };

    for (auto& [sql] : queries) {
        std::cout << "SQL: " << sql << "\n";
        auto q   = parse_select(sql);
        auto res = execute(q, students);
        print_rows(res);
        std::cout << "\n";
    }
}
