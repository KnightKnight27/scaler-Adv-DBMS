// Lab 5: Shunting-Yard expression evaluator + a minimal SQL SELECT engine
// Aditya Bhaskara (24BCS10058)
//
// Part 1 implements Dijkstra's shunting-yard algorithm. It turns an infix
// expression such as "age > 25 && salary * 1.1 < 90000" into reverse Polish
// notation, which a stack can then evaluate in a single left to right pass.
// This is exactly the machinery a database uses to evaluate a WHERE clause.
//
// Part 2 builds a tiny SELECT engine on top of it. It parses a SELECT statement
// into a small query object, then runs it over an in-memory vector<Row>:
// filter (WHERE), sort (ORDER BY), limit, then project (SELECT columns). Sorting
// runs before projection so ORDER BY can still reference a column the SELECT list
// drops, which is the same pipeline a real executor runs once the storage layer
// has handed it the rows.
//
// Build: g++ -std=c++17 -o sql_engine sql_engine.cpp
// Run:   ./sql_engine

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <map>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

// ----------------------------------------------------------------------------
// Part 1: shunting-yard
// ----------------------------------------------------------------------------

struct OpInfo {
    int  precedence;
    bool right_assoc;
};

// Lower precedence binds more loosely. Comparisons sit above the boolean
// connectives so that "a > b && c < d" groups as "(a > b) && (c < d)".
const std::unordered_map<std::string, OpInfo> kOperators = {
    {"||", {1, false}},
    {"&&", {2, false}},
    {"=",  {3, false}}, {"!=", {3, false}},
    {"<",  {4, false}}, {">",  {4, false}}, {"<=", {4, false}}, {">=", {4, false}},
    {"+",  {5, false}}, {"-",  {5, false}},
    {"*",  {6, false}}, {"/",  {6, false}},
    {"^",  {7, true}},
};

bool is_operator(const std::string& token) { return kOperators.count(token) > 0; }

std::vector<std::string> tokenize(const std::string& expr) {
    std::vector<std::string> tokens;
    const int n = static_cast<int>(expr.size());
    int i = 0;
    while (i < n) {
        char c = expr[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
        } else if (std::isdigit(static_cast<unsigned char>(c)) ||
                   (c == '.' && i + 1 < n && std::isdigit(static_cast<unsigned char>(expr[i + 1])))) {
            int j = i;
            while (j < n && (std::isdigit(static_cast<unsigned char>(expr[j])) || expr[j] == '.')) ++j;
            tokens.push_back(expr.substr(i, j - i));
            i = j;
        } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            int j = i;
            while (j < n && (std::isalnum(static_cast<unsigned char>(expr[j])) || expr[j] == '_')) ++j;
            tokens.push_back(expr.substr(i, j - i));
            i = j;
        } else if (c == '(' || c == ')') {
            tokens.emplace_back(1, c);
            ++i;
        } else if (i + 1 < n && kOperators.count(expr.substr(i, 2))) {
            tokens.push_back(expr.substr(i, 2));   // two-char operator: <=, >=, !=, &&, ||
            i += 2;
        } else {
            tokens.emplace_back(1, c);             // single-char operator
            ++i;
        }
    }
    return tokens;
}

std::vector<std::string> to_rpn(const std::vector<std::string>& tokens) {
    std::vector<std::string> output;
    std::stack<std::string>  ops;

    for (const std::string& token : tokens) {
        if (token == "(") {
            ops.push(token);
        } else if (token == ")") {
            while (!ops.empty() && ops.top() != "(") {
                output.push_back(ops.top());
                ops.pop();
            }
            if (ops.empty()) throw std::runtime_error("mismatched parentheses");
            ops.pop();                              // drop the matching "("
        } else if (is_operator(token)) {
            const OpInfo& current = kOperators.at(token);
            while (!ops.empty() && is_operator(ops.top())) {
                const OpInfo& top = kOperators.at(ops.top());
                bool top_wins = top.precedence > current.precedence ||
                                (top.precedence == current.precedence && !current.right_assoc);
                if (!top_wins) break;
                output.push_back(ops.top());
                ops.pop();
            }
            ops.push(token);
        } else {
            output.push_back(token);                // number or identifier
        }
    }
    while (!ops.empty()) {
        if (ops.top() == "(") throw std::runtime_error("mismatched parentheses");
        output.push_back(ops.top());
        ops.pop();
    }
    return output;
}

// Evaluate RPN. Identifiers are looked up in vars; everything is a double, and
// comparisons and boolean operators yield 1.0 for true and 0.0 for false.
double eval_rpn(const std::vector<std::string>& rpn,
                const std::unordered_map<std::string, double>& vars) {
    std::stack<double> stack;
    for (const std::string& token : rpn) {
        if (is_operator(token)) {
            if (stack.size() < 2) throw std::runtime_error("malformed expression");
            double b = stack.top(); stack.pop();
            double a = stack.top(); stack.pop();
            if      (token == "+")  stack.push(a + b);
            else if (token == "-")  stack.push(a - b);
            else if (token == "*")  stack.push(a * b);
            else if (token == "/")  stack.push(a / b);
            else if (token == "^")  stack.push(std::pow(a, b));
            else if (token == "<")  stack.push(a <  b ? 1.0 : 0.0);
            else if (token == ">")  stack.push(a >  b ? 1.0 : 0.0);
            else if (token == "<=") stack.push(a <= b ? 1.0 : 0.0);
            else if (token == ">=") stack.push(a >= b ? 1.0 : 0.0);
            else if (token == "=")  stack.push(a == b ? 1.0 : 0.0);
            else if (token == "!=") stack.push(a != b ? 1.0 : 0.0);
            else if (token == "&&") stack.push((a != 0.0 && b != 0.0) ? 1.0 : 0.0);
            else if (token == "||") stack.push((a != 0.0 || b != 0.0) ? 1.0 : 0.0);
        } else {
            try {
                size_t consumed = 0;
                double literal = std::stod(token, &consumed);
                if (consumed == token.size()) { stack.push(literal); continue; }
            } catch (const std::exception&) {
                // not a number, fall through to a variable lookup
            }
            auto it = vars.find(token);
            if (it == vars.end()) throw std::runtime_error("unknown identifier: " + token);
            stack.push(it->second);
        }
    }
    if (stack.size() != 1) throw std::runtime_error("malformed expression");
    return stack.top();
}

void shunting_yard_demo() {
    std::cout << "=== Part 1: shunting-yard ===\n";
    const std::string expr = "age * 2 + salary / 1000 > 100 && age < 40";
    auto rpn = to_rpn(tokenize(expr));

    std::cout << "infix : " << expr << "\n";
    std::cout << "rpn   : ";
    for (const std::string& t : rpn) std::cout << t << " ";
    std::cout << "\n";

    std::unordered_map<std::string, double> vars = {{"age", 30}, {"salary", 50000}};
    std::cout << "result: " << (eval_rpn(rpn, vars) != 0.0 ? "true" : "false") << "\n\n";
}

// ----------------------------------------------------------------------------
// Part 2: minimal SQL SELECT engine
// ----------------------------------------------------------------------------

using Value = std::variant<double, std::string>;

struct Row {
    std::map<std::string, Value> cols;          // map keeps column output ordered
};

struct SelectQuery {
    std::vector<std::string> columns;           // empty means SELECT *
    std::string              from;
    std::string              where;             // raw clause, evaluated per row
    std::string              order_by;
    bool                     order_asc = true;
    int                      limit = -1;
};

double as_double(const Value& v) {
    if (auto* d = std::get_if<double>(&v)) return *d;
    if (auto* s = std::get_if<std::string>(&v)) {
        try { return std::stod(*s); } catch (const std::exception&) {}
    }
    return 0.0;
}

std::string as_string(const Value& v) {
    if (auto* s = std::get_if<std::string>(&v)) return *s;
    std::ostringstream os;
    os << std::get<double>(v);
    return os.str();
}

double column_as_double(const Row& row, const std::string& name) {
    auto it = row.cols.find(name);
    return it == row.cols.end() ? 0.0 : as_double(it->second);
}

std::string to_upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// Split on whitespace. The expression tokenizer later re-splits the WHERE clause,
// so tight operators like "gpa>3.0" survive being one word here.
std::vector<std::string> split_words(const std::string& sql) {
    std::istringstream ss(sql);
    std::vector<std::string> words;
    std::string word;
    while (ss >> word) words.push_back(word);
    return words;
}

SelectQuery parse_select(const std::string& sql) {
    std::vector<std::string> words = split_words(sql);
    SelectQuery q;
    size_t i = 0;

    if (i >= words.size() || to_upper(words[i]) != "SELECT")
        throw std::runtime_error("query must start with SELECT");
    ++i;

    // Column list up to FROM. Join the words and split on commas so that both
    // "id, name" and "id,name" parse the same way.
    std::string column_blob;
    while (i < words.size() && to_upper(words[i]) != "FROM")
        column_blob += words[i++] + " ";
    std::stringstream cs(column_blob);
    std::string col;
    while (std::getline(cs, col, ',')) {
        std::stringstream trim(col);
        trim >> col;                              // strip surrounding whitespace
        if (col == "*" || col.empty()) continue;  // "*" leaves columns empty
        q.columns.push_back(col);
    }

    if (i >= words.size() || to_upper(words[i]) != "FROM")
        throw std::runtime_error("expected FROM");
    ++i;
    if (i < words.size()) q.from = words[i++];

    // Optional WHERE / ORDER BY / LIMIT clauses, in any order.
    while (i < words.size()) {
        std::string kw = to_upper(words[i]);
        if (kw == "WHERE") {
            ++i;
            std::string clause;
            while (i < words.size() &&
                   to_upper(words[i]) != "ORDER" && to_upper(words[i]) != "LIMIT")
                clause += words[i++] + " ";
            q.where = clause;
        } else if (kw == "ORDER") {
            ++i;
            if (i < words.size() && to_upper(words[i]) == "BY") ++i;
            if (i < words.size()) q.order_by = words[i++];
            if (i < words.size() && to_upper(words[i]) == "DESC") { q.order_asc = false; ++i; }
            else if (i < words.size() && to_upper(words[i]) == "ASC") ++i;
        } else if (kw == "LIMIT") {
            ++i;
            if (i < words.size()) q.limit = std::stoi(words[i++]);
        } else {
            ++i;                                  // skip anything we do not model
        }
    }
    return q;
}

bool value_less(const Value& a, const Value& b) {
    bool both_numeric = std::holds_alternative<double>(a) && std::holds_alternative<double>(b);
    if (both_numeric) return std::get<double>(a) < std::get<double>(b);
    return as_string(a) < as_string(b);
}

std::vector<Row> execute(const SelectQuery& q, const std::vector<Row>& table) {
    std::vector<std::string> where_rpn;
    if (!q.where.empty()) where_rpn = to_rpn(tokenize(q.where));

    // 1. WHERE: keep the full rows so the steps below can still see every column.
    std::vector<Row> rows;
    for (const Row& row : table) {
        if (!where_rpn.empty()) {
            std::unordered_map<std::string, double> vars;
            for (const auto& [name, value] : row.cols) vars[name] = as_double(value);
            if (eval_rpn(where_rpn, vars) == 0.0) continue;
        }
        rows.push_back(row);
    }

    // 2. ORDER BY runs before projection so it can sort on a column that the
    //    SELECT list does not include, such as "SELECT name ... ORDER BY age".
    if (!q.order_by.empty()) {
        const std::string& key = q.order_by;
        bool asc = q.order_asc;
        std::stable_sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) {
            auto ia = a.cols.find(key);
            auto ib = b.cols.find(key);
            if (ia == a.cols.end() || ib == b.cols.end()) return false;
            return asc ? value_less(ia->second, ib->second)
                       : value_less(ib->second, ia->second);
        });
    }

    // 3. LIMIT.
    if (q.limit >= 0 && static_cast<int>(rows.size()) > q.limit)
        rows.resize(q.limit);

    // 4. SELECT projection happens last, once ordering no longer needs the
    //    dropped columns.
    if (q.columns.empty()) return rows;

    std::vector<Row> projected;
    projected.reserve(rows.size());
    for (const Row& row : rows) {
        Row out;
        for (const std::string& name : q.columns) {
            auto it = row.cols.find(name);
            if (it != row.cols.end()) out.cols[name] = it->second;
        }
        projected.push_back(std::move(out));
    }
    return projected;
}

void print_rows(const std::vector<Row>& rows) {
    if (rows.empty()) {
        std::cout << "  (no rows)\n";
        return;
    }
    for (const Row& row : rows) {
        std::cout << "  ";
        for (const auto& [name, value] : row.cols)
            std::cout << name << "=" << as_string(value) << "  ";
        std::cout << "\n";
    }
}

void select_engine_demo() {
    std::cout << "=== Part 2: SELECT engine ===\n";

    const std::vector<Row> students = {
        {{{"id", 1.0}, {"name", std::string("Alice")}, {"age", 22.0}, {"gpa", 3.8}}},
        {{{"id", 2.0}, {"name", std::string("Bob")},   {"age", 25.0}, {"gpa", 2.9}}},
        {{{"id", 3.0}, {"name", std::string("Carol")}, {"age", 21.0}, {"gpa", 3.5}}},
        {{{"id", 4.0}, {"name", std::string("Dave")},  {"age", 30.0}, {"gpa", 3.1}}},
        {{{"id", 5.0}, {"name", std::string("Eve")},   {"age", 28.0}, {"gpa", 3.9}}},
    };

    const std::vector<std::string> queries = {
        "SELECT id, name, gpa FROM students WHERE gpa > 3.0 ORDER BY gpa DESC LIMIT 3",
        "SELECT * FROM students WHERE age >= 22 && age <= 28",
        "SELECT name, age FROM students ORDER BY name ASC",
    };

    for (const std::string& sql : queries) {
        std::cout << "sql: " << sql << "\n";
        print_rows(execute(parse_select(sql), students));
        std::cout << "\n";
    }
}

int main() {
    shunting_yard_demo();
    select_engine_demo();
    return 0;
}
