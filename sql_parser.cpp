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
// Operator Metadata & Expression Evaluator (Shunting-Yard)
// ============================================================================

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
                throw std::runtime_error("Malformed expression: insufficient operands for " + tok);
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
    if (stk.empty()) throw std::runtime_error("Empty expression evaluation stack");
    return stk.top();
}

// Demo function for Shunting-Yard
void shunting_demo() {
    std::string expr = "age * 2 + salary / 1000 > 100";
    auto tokens = tokenize(expr);
    auto rpn    = to_rpn(tokens);

    std::cout << "--- Shunting-Yard Demo ---\n";
    std::cout << "Expression : " << expr << "\n";
    std::cout << "RPN        : ";
    for (auto& t : rpn) std::cout << t << " ";
    std::cout << "\n";

    std::unordered_map<std::string, double> vars = {{"age", 30}, {"salary", 50000}};
    double result = eval_rpn(rpn, vars);
    std::cout << "Result     : " << (result ? "true" : "false") << "\n\n";
}

// ============================================================================
// SQL Parser & In-Memory Executor
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

// Helper to convert string to uppercase
std::string to_upper(std::string s) {
    for (auto& c : s) c = std::toupper(c);
    return s;
}

// Robust SQL Tokenizer
std::vector<std::string> sql_tokenize(const std::string& sql) {
    std::vector<std::string> tokens;
    int i = 0, n = sql.size();
    while (i < n) {
        if (std::isspace(sql[i])) { i++; continue; }
        if (sql[i] == '\'') {
            int j = i + 1;
            while (j < n && sql[j] != '\'') j++;
            if (j < n) {
                tokens.push_back(sql.substr(i, j - i + 1));
                i = j + 1;
            } else {
                tokens.push_back(sql.substr(i));
                i = n;
            }
        } else if (sql[i] == ',' || sql[i] == '*' || sql[i] == '(' || sql[i] == ')') {
            tokens.push_back(std::string(1, sql[i++]));
        } else {
            // Read word until whitespace or standard SQL punctuation/separators
            int j = i;
            while (j < n && !std::isspace(sql[j]) && sql[j] != ',' && sql[j] != '*' && sql[j] != '\'' && sql[j] != '(' && sql[j] != ')') {
                j++;
            }
            tokens.push_back(sql.substr(i, j - i));
            i = j;
        }
    }
    return tokens;
}

// Robust SQL SELECT Parser
SelectQuery parse_select(const std::string& sql) {
    SelectQuery q;
    auto tokens = sql_tokenize(sql);
    if (tokens.empty()) return q;
    
    int idx = 0;
    int n = tokens.size();
    
    // Expect SELECT keyword
    if (to_upper(tokens[idx]) != "SELECT") {
        throw std::runtime_error("Expected SELECT keyword");
    }
    idx++;
    
    // Read projection column list until FROM keyword
    while (idx < n && to_upper(tokens[idx]) != "FROM") {
        if (tokens[idx] == ",") {
            idx++;
            continue;
        }
        if (tokens[idx] == "*") {
            q.columns.clear();
        } else {
            q.columns.push_back(tokens[idx]);
        }
        idx++;
    }
    
    if (idx >= n || to_upper(tokens[idx]) != "FROM") {
        throw std::runtime_error("Expected FROM keyword");
    }
    idx++; // skip FROM
    
    if (idx < n) {
        q.from = tokens[idx];
        idx++;
    }
    
    // Parse optional clauses: WHERE, ORDER BY, LIMIT
    while (idx < n) {
        std::string kw = to_upper(tokens[idx]);
        if (kw == "WHERE") {
            idx++;
            std::string clause;
            while (idx < n) {
                std::string next_kw = to_upper(tokens[idx]);
                if (next_kw == "ORDER" || next_kw == "LIMIT") {
                    break;
                }
                clause += (clause.empty() ? "" : " ") + tokens[idx];
                idx++;
            }
            q.where_raw = clause;
        } else if (kw == "ORDER") {
            idx++; // skip ORDER
            if (idx < n && to_upper(tokens[idx]) == "BY") {
                idx++; // skip BY
            } else {
                throw std::runtime_error("Expected BY after ORDER");
            }
            if (idx < n) {
                q.order_by = tokens[idx];
                idx++;
            }
            if (idx < n) {
                std::string dir = to_upper(tokens[idx]);
                if (dir == "DESC") {
                    q.order_asc = false;
                    idx++;
                } else if (dir == "ASC") {
                    q.order_asc = true;
                    idx++;
                }
            }
        } else if (kw == "LIMIT") {
            idx++; // skip LIMIT
            if (idx < n) {
                q.limit = std::stoi(tokens[idx]);
                idx++;
            }
        } else {
            idx++; // Unrecognized keywords are skipped
        }
    }
    return q;
}

// SQL Query Executor (Correct Logical order: Filter -> Sort -> Limit -> Project)
std::vector<Row> execute(const SelectQuery& q, const std::vector<Row>& data) {
    std::vector<std::string> rpn;
    if (!q.where_raw.empty())
        rpn = to_rpn(tokenize(q.where_raw));

    std::vector<Row> result;

    // 1. FILTER (WHERE clause execution)
    for (const auto& row : data) {
        if (!rpn.empty()) {
            std::unordered_map<std::string, double> vars;
            for (auto& [k, v] : row.cols) vars[k] = row_val(row, k);
            if (!eval_rpn(rpn, vars)) continue;
        }
        result.push_back(row);
    }

    // 2. SORT (ORDER BY clause execution)
    if (!q.order_by.empty()) {
        std::sort(result.begin(), result.end(), [&](const Row& a, const Row& b) {
            double va = row_val(a, q.order_by);
            double vb = row_val(b, q.order_by);
            if (va == vb) return false;
            return q.order_asc ? va < vb : va > vb;
        });
    }

    // 3. LIMIT (Truncate result set)
    if (q.limit >= 0 && (int)result.size() > q.limit) {
        result.resize(q.limit);
    }

    // 4. PROJECT (SELECT columns projection)
    if (!q.columns.empty()) {
        std::vector<Row> projected_result;
        for (const auto& row : result) {
            Row projected;
            for (const auto& col : q.columns) {
                if (row.cols.count(col)) {
                    projected.cols[col] = row.cols.at(col);
                }
            }
            projected_result.push_back(projected);
        }
        result = std::move(projected_result);
    }

    return result;
}

// Helper to print rows nicely
void print_rows(const std::vector<Row>& rows) {
    if (rows.empty()) {
        std::cout << "(No rows returned)\n";
        return;
    }
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

// ============================================================================
// Main & Test Suite
// ============================================================================

int main() {
    // Run the basic Shunting-Yard demo
    shunting_demo();

    // In-memory data mimicking a students table
    std::vector<Row> students = {
        {{{ "id", 1.0 }, { "name", std::string("Alice") }, { "age", 22.0 }, { "gpa", 3.8 }}},
        {{{ "id", 2.0 }, { "name", std::string("Bob")   }, { "age", 25.0 }, { "gpa", 2.9 }}},
        {{{ "id", 3.0 }, { "name", std::string("Carol")  }, { "age", 21.0 }, { "gpa", 3.5 }}},
        {{{ "id", 4.0 }, { "name", std::string("Dave")   }, { "age", 30.0 }, { "gpa", 3.1 }}},
    };

    std::cout << "--- DB Demo ---\n";
    std::cout << "Initial Students Dataset:\n";
    print_rows(students);
    std::cout << "\n";

    // Test queries representing standard cases, edge cases, and improvements
    struct TestQuery {
        std::string description;
        std::string sql;
    } queries[] = {
        {
            "Standard Query: project subset of columns, filter GPA, sort descending, limit",
            "SELECT id, name, gpa FROM students WHERE gpa > 3.0 ORDER BY gpa DESC LIMIT 3"
        },
        {
            "Logical operator (&&) and SELECT *:",
            "SELECT * FROM students WHERE age >= 22 && age <= 26"
        },
        {
            "No spaces around columns (Robust Column parsing verification):",
            "SELECT id,name,gpa FROM students"
        },
        {
            "Ordering by a column NOT in the projection list (Order/Project precedence bug fix verification):",
            "SELECT name FROM students ORDER BY age DESC LIMIT 2"
        },
        {
            "No WHERE clause but sorting and limiting:",
            "SELECT id, name FROM students ORDER BY gpa ASC LIMIT 2"
        }
    };

    for (auto& q_test : queries) {
        std::cout << "Test Case: " << q_test.description << "\n";
        std::cout << "SQL: " << q_test.sql << "\n";
        try {
            auto q   = parse_select(q_test.sql);
            auto res = execute(q, students);
            print_rows(res);
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << "\n";
        }
        std::cout << "\n";
    }

    return 0;
}
