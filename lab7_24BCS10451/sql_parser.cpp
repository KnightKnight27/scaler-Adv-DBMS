// Lab 7: Shunting-Yard expression evaluator + minimal SQL SELECT parser. 24BCS10451
// Build: g++ -std=c++17 -o sql_parser sql_parser.cpp && ./sql_parser

#include <iostream>
#include <sstream>
#include <stack>
#include <vector>
#include <string>
#include <unordered_map>
#include <variant>
#include <algorithm>
#include <cctype>

//  Shunting-Yard

// precedence for each operator; higher binds tighter
int prec(const std::string& op) {
    if (op == "||") return 1;
    if (op == "&&") return 2;
    if (op == "=" || op == "!=") return 3;
    if (op == "<" || op == ">" || op == "<=" || op == ">=") return 4;
    if (op == "+" || op == "-") return 5;
    if (op == "*" || op == "/") return 6;
    return 0;
}

bool is_op(const std::string& t) { return prec(t) > 0; }

std::vector<std::string> tokenize(const std::string& expr) {
    std::vector<std::string> tokens;
    size_t i = 0;
    while (i < expr.size()) {
        char c = expr[i];
        if (std::isspace((unsigned char)c)) {
            i++;
        } else if (std::isdigit((unsigned char)c) || c == '.') {
            size_t j = i;
            while (j < expr.size() && (std::isdigit((unsigned char)expr[j]) || expr[j] == '.')) j++;
            tokens.push_back(expr.substr(i, j - i));
            i = j;
        } else if (std::isalpha((unsigned char)c) || c == '_') {
            size_t j = i;
            while (j < expr.size() && (std::isalnum((unsigned char)expr[j]) || expr[j] == '_')) j++;
            tokens.push_back(expr.substr(i, j - i));
            i = j;
        } else if (c == '(' || c == ')') {
            tokens.push_back(std::string(1, c));
            i++;
        } else {
            // two char operators
            std::string two = expr.substr(i, 2);
            if (two == "!=" || two == "<=" || two == ">=" || two == "&&" || two == "||") {
                tokens.push_back(two);
                i += 2;
            } else {
                tokens.push_back(std::string(1, c));
                i++;
            }
        }
    }
    return tokens;
}

// infix tokens -> postfix (RPN)
std::vector<std::string> to_rpn(const std::vector<std::string>& tokens) {
    std::vector<std::string> out;
    std::stack<std::string> ops;
    for (const auto& t : tokens) {
        if (t == "(") {
            ops.push(t);
        } else if (t == ")") {
            while (!ops.empty() && ops.top() != "(") {
                out.push_back(ops.top());
                ops.pop();
            }
            if (!ops.empty()) ops.pop(); // drop '('
        } else if (is_op(t)) {
            while (!ops.empty() && is_op(ops.top()) && prec(ops.top()) >= prec(t)) {
                out.push_back(ops.top());
                ops.pop();
            }
            ops.push(t);
        } else {
            out.push_back(t); // number or column name
        }
    }
    while (!ops.empty()) {
        out.push_back(ops.top());
        ops.pop();
    }
    return out;
}

// evaluate RPN; column names are looked up in vars
double eval_rpn(const std::vector<std::string>& rpn,
                const std::unordered_map<std::string, double>& vars) {
    std::stack<double> st;
    for (const auto& t : rpn) {
        if (is_op(t)) {
            double b = st.top(); st.pop();
            double a = st.top(); st.pop();
            if      (t == "+")  st.push(a + b);
            else if (t == "-")  st.push(a - b);
            else if (t == "*")  st.push(a * b);
            else if (t == "/")  st.push(a / b);
            else if (t == "<")  st.push(a <  b);
            else if (t == ">")  st.push(a >  b);
            else if (t == "<=") st.push(a <= b);
            else if (t == ">=") st.push(a >= b);
            else if (t == "=")  st.push(a == b);
            else if (t == "!=") st.push(a != b);
            else if (t == "&&") st.push(a && b);
            else if (t == "||") st.push(a || b);
        } else if (std::isdigit((unsigned char)t[0]) || t[0] == '.') {
            st.push(std::stod(t));
        } else {
            auto it = vars.find(t);
            st.push(it == vars.end() ? 0.0 : it->second);
        }
    }
    return st.top();
}

void shunting_demo() {
    std::string expr = "age * 2 + salary / 1000 > 100";
    auto rpn = to_rpn(tokenize(expr));

    std::cout << "Expression : " << expr << "\n";
    std::cout << "RPN        : ";
    for (auto& t : rpn) std::cout << t << " ";
    std::cout << "\n";

    std::unordered_map<std::string, double> vars = {{"age", 30}, {"salary", 50000}};
    std::cout << "Result     : " << (eval_rpn(rpn, vars) ? "true" : "false") << "\n\n";
}

// ---------- Part 2: SQL SELECT over vector<Row> ----------

using Value = std::variant<double, std::string>;

struct Row {
    std::unordered_map<std::string, Value> cols;
};

double row_val(const Row& row, const std::string& col) {
    auto it = row.cols.find(col);
    if (it == row.cols.end()) return 0.0;
    if (auto* d = std::get_if<double>(&it->second)) return *d;
    return 0.0; // strings count as 0 in arithmetic
}

struct SelectQuery {
    std::vector<std::string> columns; // empty = SELECT *
    std::string from;
    std::string where;
    std::string order_by;
    bool order_asc = true;
    int  limit = -1;
};

std::string upper(std::string s) {
    for (auto& c : s) c = std::toupper((unsigned char)c);
    return s;
}

SelectQuery parse_select(const std::string& sql) {
    SelectQuery q;
    std::istringstream ss(sql);
    std::string w;

    ss >> w; // SELECT

    // columns until FROM
    while (ss >> w && upper(w) != "FROM") {
        if (!w.empty() && w.back() == ',') w.pop_back();
        if (w != "*") q.columns.push_back(w);
    }

    ss >> q.from;

    // remaining clauses
    while (ss >> w) {
        std::string kw = upper(w);
        if (kw == "WHERE") {
            while (ss >> w) {
                kw = upper(w);
                if (kw == "ORDER" || kw == "LIMIT") break;
                q.where += (q.where.empty() ? "" : " ") + w;
            }
        }
        if (kw == "ORDER") {
            ss >> w;             // BY
            ss >> q.order_by;
            std::string dir;
            if (ss >> dir && upper(dir) == "DESC") q.order_asc = false;
        }
        if (kw == "LIMIT") {
            ss >> q.limit;
        }
    }
    return q;
}

std::vector<Row> execute(const SelectQuery& q, const std::vector<Row>& data) {
    std::vector<std::string> rpn;
    if (!q.where.empty()) rpn = to_rpn(tokenize(q.where));

    std::vector<Row> result;
    for (const auto& row : data) {
        if (!rpn.empty()) {
            std::unordered_map<std::string, double> vars;
            for (auto& [k, v] : row.cols) vars[k] = row_val(row, k);
            if (!eval_rpn(rpn, vars)) continue;
        }
        if (q.columns.empty()) {
            result.push_back(row);
        } else {
            Row r;
            for (auto& c : q.columns)
                if (row.cols.count(c)) r.cols[c] = row.cols.at(c);
            result.push_back(r);
        }
    }

    if (!q.order_by.empty()) {
        std::sort(result.begin(), result.end(), [&](const Row& a, const Row& b) {
            double va = row_val(a, q.order_by), vb = row_val(b, q.order_by);
            return q.order_asc ? va < vb : va > vb;
        });
    }

    if (q.limit >= 0 && (int)result.size() > q.limit)
        result.resize(q.limit);

    return result;
}

void print_rows(const std::vector<Row>& rows) {
    for (const auto& row : rows) {
        for (const auto& [k, v] : row.cols) {
            std::cout << k << "=";
            if (auto* d = std::get_if<double>(&v)) std::cout << *d;
            else                                   std::cout << std::get<std::string>(v);
            std::cout << "  ";
        }
        std::cout << "\n";
    }
}

// ---------- Demo ----------

int main() {
    shunting_demo();

    std::vector<Row> students = {
        {{{"id", 1.0}, {"name", std::string("Alice")}, {"age", 22.0}, {"gpa", 3.8}}},
        {{{"id", 2.0}, {"name", std::string("Bob")},   {"age", 25.0}, {"gpa", 2.9}}},
        {{{"id", 3.0}, {"name", std::string("Carol")}, {"age", 21.0}, {"gpa", 3.5}}},
        {{{"id", 4.0}, {"name", std::string("Dave")},  {"age", 30.0}, {"gpa", 3.1}}},
    };

    std::vector<std::string> queries = {
        "SELECT id, name, gpa FROM students WHERE gpa > 3.0 ORDER BY gpa DESC LIMIT 3",
        "SELECT * FROM students WHERE age >= 22 && age <= 26",
    };

    for (const auto& sql : queries) {
        std::cout << "SQL: " << sql << "\n";
        print_rows(execute(parse_select(sql), students));
        std::cout << "\n";
    }
}
