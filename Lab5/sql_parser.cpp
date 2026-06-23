#include <iostream>
#include <sstream>
#include <stack>
#include <vector>
#include <string>
#include <unordered_map>
#include <stdexcept>
#include <cctype>
#include <variant>
#include <algorithm>
#include <functional>
#include <cmath>

// I defined the operator metadata here to help with precedence and associativity
struct OpInfo { int precedence; bool right_assoc; };

const std::unordered_map<std::string, OpInfo> OPS = {
    {"||", {1, false}},   // logical OR
    {"&&", {2, false}},   // logical AND
    {"=",  {3, false}},   // equality check
    {"!=", {3, false}},
    {"<",  {4, false}},
    {">",  {4, false}},
    {"<=", {4, false}},
    {">=", {4, false}},
    {"+",  {5, false}},
    {"-",  {5, false}},
    {"*",  {6, false}},
    {"/",  {6, false}},
    {"^",  {7, true }},   // right-associative exponentiation
};

// I wrote this tokenizer to split out numbers, identifiers, operators, and parentheses
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
            // Check for two-character operators first
            if (i+1 < n) {
                std::string two = expr.substr(i, 2);
                if (OPS.count(two)) { tokens.push_back(two); i += 2; continue; }
            }
            tokens.push_back(std::string(1, expr[i++]));
        }
    }
    return tokens;
}

// I implemented the Shunting-Yard algorithm here to convert infix tokens into postfix (RPN)
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
            if (ops.empty()) throw std::runtime_error("Mismatched parentheses found in expression");
            ops.pop(); // throw away the '('
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
            output.push_back(tok); // It must be a number or an identifier
        }
    }
    while (!ops.empty()) {
        if (ops.top() == "(") throw std::runtime_error("Mismatched parentheses found in expression");
        output.push_back(ops.top()); ops.pop();
    }
    return output;
}

// I set up this RPN evaluator, using a variable map so I can treat all values as doubles to keep it simple
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
                if (it == vars.end())
                    throw std::runtime_error("Unknown variable encountered: " + tok);
                stk.push(it->second);
            }
        }
    }
    return stk.top();
}

// Just a quick demo I added to verify the shunting-yard logic
void shunting_demo() {
    std::string expr = "age * 2 + salary / 1000 > 100";
    auto tokens = tokenize(expr);
    auto rpn    = to_rpn(tokens);

    std::cout << "Testing Expression : " << expr << "\n";
    std::cout << "RPN Output         : ";
    for (auto& t : rpn) std::cout << t << " ";
    std::cout << "\n";

    std::unordered_map<std::string, double> vars = {{"age", 30}, {"salary", 50000}};
    double result = eval_rpn(rpn, vars);
    std::cout << "Evaluation Result  : " << (result ? "true" : "false") << "\n\n";
}

// For Part 2, I defined the Row type to hold variant columns
using Value = std::variant<double, std::string>;

struct Row {
    std::unordered_map<std::string, Value> cols;
};

// I created this helper to extract numeric values from a row column, useful when I evaluate expressions
double row_val(const Row& row, const std::string& col) {
    auto it = row.cols.find(col);
    if (it == row.cols.end()) return 0.0;
    if (auto* d = std::get_if<double>(&it->second)) return *d;
    if (auto* s = std::get_if<std::string>(&it->second)) {
        try { return std::stod(*s); } catch (...) {}
    }
    return 0.0;
}

// I use this to store the parsed SQL representation
struct SelectQuery {
    std::vector<std::string>  columns;   // an empty vector here means SELECT *
    std::string               from;      // I'll ignore the table name for now since I'm using pre-fetched data
    std::string               where_raw; // just storing the raw WHERE clause string
    std::string               order_by;  // what column to order by
    bool                      order_asc = true;
    int                       limit = -1;
};

// I needed a quick way to convert strings to uppercase for SQL keyword matching
std::string to_upper(std::string s) {
    for (auto& c : s) c = std::toupper(c);
    return s;
}

// I wrote this minimal parser to grab the SELECT parts
SelectQuery parse_select(const std::string& sql) {
    SelectQuery q;
    std::istringstream ss(sql);
    std::string word;
    ss >> word; // grab SELECT

    // I loop to read the column list until hitting FROM
    std::string col_buf;
    while (ss >> word && to_upper(word) != "FROM") {
        // I want to strip off any trailing commas
        if (!word.empty() && word.back() == ',') word.pop_back();
        if (word == "*") q.columns.clear();
        else             q.columns.push_back(word);
    }

    ss >> q.from; // table name goes here

    // I read the optional clauses in this loop
    while (ss >> word) {
        std::string kw = to_upper(word);
        if (kw == "WHERE") {
            // Consume everything until I hit ORDER or LIMIT or the end
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
            ss >> word; // consume BY
            ss >> q.order_by;
            std::string dir;
            if (ss >> dir && to_upper(dir) == "DESC") q.order_asc = false;
        }
        if (kw == "LIMIT") {
            ss >> q.limit;
        }
    }
    return q;
}

// Here's the executor I built. It takes the parsed query and the rows
std::vector<Row> execute(const SelectQuery& q, const std::vector<Row>& data) {
    std::vector<std::string> rpn;
    if (!q.where_raw.empty())
        rpn = to_rpn(tokenize(q.where_raw));

    std::vector<Row> result;

    for (const auto& row : data) {
        // Evaluate my WHERE conditions
        if (!rpn.empty()) {
            // I build a variable map from the current row
            std::unordered_map<std::string, double> vars;
            for (auto& [k, v] : row.cols) vars[k] = row_val(row, k);
            if (!eval_rpn(rpn, vars)) continue; // skip row if it evaluates to false
        }

        // Project the required columns
        if (q.columns.empty()) {
            result.push_back(row);
        } else {
            Row projected;
            for (auto& col : q.columns)
                if (row.cols.count(col)) projected.cols[col] = row.cols.at(col);
            result.push_back(projected);
        }
    }

    // Handle ORDER BY sorting
    if (!q.order_by.empty()) {
        std::sort(result.begin(), result.end(), [&](const Row& a, const Row& b) {
            double va = row_val(a, q.order_by);
            double vb = row_val(b, q.order_by);
            return q.order_asc ? va < vb : va > vb;
        });
    }

    // Apply the LIMIT
    if (q.limit >= 0 && (int)result.size() > q.limit)
        result.resize(q.limit);

    return result;
}

// I added this helper to easily print out the resulting rows
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
    // Run my quick shunting demo first
    shunting_demo();

    // I set up some pre-fetched mock data to act like output from the storage layer
    std::vector<Row> students = {
        {{{ "id", 1.0 }, { "name", std::string("Alice") }, { "age", 22.0 }, { "gpa", 3.8 }}},
        {{{ "id", 2.0 }, { "name", std::string("Bob")   }, { "age", 25.0 }, { "gpa", 2.9 }}},
        {{{ "id", 3.0 }, { "name", std::string("Carol")  }, { "age", 21.0 }, { "gpa", 3.5 }}},
        {{{ "id", 4.0 }, { "name", std::string("Dave")   }, { "age", 30.0 }, { "gpa", 3.1 }}},
    };

    // Let's test a couple of queries against the mock data
    struct { std::string sql; } queries[] = {
        { "SELECT id, name, gpa FROM students WHERE gpa > 3.0 ORDER BY gpa DESC LIMIT 3" },
        { "SELECT * FROM students WHERE age >= 22 && age <= 26" },
    };

    for (auto& [sql] : queries) {
        std::cout << "Running SQL: " << sql << "\n";
        auto q   = parse_select(sql);
        auto res = execute(q, students);
        print_rows(res);
        std::cout << "\n";
    }
}
