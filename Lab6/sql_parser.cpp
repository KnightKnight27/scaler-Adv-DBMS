#include <iostream>
#include <sstream>
#include <stack>
#include <vector>
#include <string>
#include <unordered_map>
#include <stdexcept>
#include <cctype>
#include <algorithm>
#include <cmath>

// ─── Operator metadata ────────────────────────────────────────────────────────

struct OpInfo { int precedence; bool right_assoc; };

const std::unordered_map<std::string, OpInfo> OPS = {
    {"||", {1, false}},
    {"&&", {2, false}},
    {"=",  {3, false}},
    {"!=", {3, false}},
    {"<",  {4, false}},
    {">",  {4, false}},
    {"<=", {4, false}},
    {">=", {4, false}},
    {"+",  {5, false}},
    {"-",  {5, false}},
    {"*",  {6, false}},
    {"/",  {6, false}},
    {"^",  {7, true }},
};

// ─── Tokenizer ────────────────────────────────────────────────────────────────

std::vector<std::string> tokenize(const std::string& expr) {
    std::vector<std::string> tokens;
    int i = 0, n = expr.size();
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
            if (i+1 < n) {
                std::string two = expr.substr(i, 2);
                if (OPS.count(two)) { tokens.push_back(two); i += 2; continue; }
            }
            tokens.push_back(std::string(1, expr[i++]));
        }
    }
    return tokens;
}

// ─── Shunting-Yard: infix → RPN ───────────────────────────────────────────────

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
            ops.pop();
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
            output.push_back(tok);
        }
    }
    while (!ops.empty()) {
        if (ops.top() == "(") throw std::runtime_error("Mismatched parentheses");
        output.push_back(ops.top()); ops.pop();
    }
    return output;
}

// ─── RPN Evaluator ────────────────────────────────────────────────────────────

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
            else if (tok == "<")  stk.push(a <  b ? 1.0 : 0.0);
            else if (tok == ">")  stk.push(a >  b ? 1.0 : 0.0);
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
    return stk.top();
}

void shunting_demo() {
    std::cout << "=== Part 1: Shunting-Yard Algorithm Demo ===\n\n";

    std::string expr = "age * 2 + salary / 1000 > 100";
    auto tokens = tokenize(expr);
    auto rpn    = to_rpn(tokens);

    std::cout << "Expression : " << expr << "\n";
    std::cout << "RPN        : ";
    for (auto& t : rpn) std::cout << t << " ";
    std::cout << "\n";

    std::unordered_map<std::string, double> vars = {{"age", 30.0}, {"salary", 50000.0}};
    double result = eval_rpn(rpn, vars);
    std::cout << "Variables  : age=30, salary=50000\n";
    std::cout << "Result     : " << (result ? "true" : "false") << "\n\n";

    std::string expr2 = "(3 + 4) * 2 ^ 3 - 1";
    auto rpn2 = to_rpn(tokenize(expr2));
    std::cout << "Expression : " << expr2 << "\n";
    std::cout << "RPN        : ";
    for (auto& t : rpn2) std::cout << t << " ";
    std::cout << "\n";
    std::cout << "Result     : " << eval_rpn(rpn2, {}) << "\n\n";
}

// ─── Row / Value types ────────────────────────────────────────────────────────

enum class ValType { NUM, STR };

struct Value {
    ValType type;
    double      num;
    std::string str;

    static Value from_num(double d) { Value v; v.type = ValType::NUM; v.num = d; return v; }
    static Value from_str(const std::string& s) { Value v; v.type = ValType::STR; v.str = s; return v; }
};

struct Row {
    std::unordered_map<std::string, Value> cols;
};

double row_val(const Row& row, const std::string& col) {
    auto it = row.cols.find(col);
    if (it == row.cols.end()) return 0.0;
    if (it->second.type == ValType::NUM) return it->second.num;
    try { return std::stod(it->second.str); } catch (...) {}
    return 0.0;
}

// ─── SQL Query structure ──────────────────────────────────────────────────────

struct SelectQuery {
    std::vector<std::string> columns;
    std::string              from;
    std::string              where_raw;
    std::string              order_by;
    bool                     order_asc = true;
    int                      limit = -1;
};

std::string to_upper(std::string s) {
    for (auto& c : s) c = std::toupper((unsigned char)c);
    return s;
}

// ─── Parser ───────────────────────────────────────────────────────────────────

SelectQuery parse_select(const std::string& sql) {
    SelectQuery q;
    std::istringstream ss(sql);
    std::string word;
    ss >> word;

    while (ss >> word && to_upper(word) != "FROM") {
        if (!word.empty() && word.back() == ',') word.pop_back();
        if (word == "*") q.columns.clear();
        else             q.columns.push_back(word);
    }

    ss >> q.from;

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
            ss >> word;
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

// ─── Executor ─────────────────────────────────────────────────────────────────

std::vector<Row> execute(const SelectQuery& q, const std::vector<Row>& data) {
    std::vector<std::string> rpn;
    if (!q.where_raw.empty())
        rpn = to_rpn(tokenize(q.where_raw));

    std::vector<Row> result;

    for (const auto& row : data) {
        if (!rpn.empty()) {
            std::unordered_map<std::string, double> vars;
            for (const auto& kv : row.cols) vars[kv.first] = row_val(row, kv.first);
            if (!eval_rpn(rpn, vars)) continue;
        }

        if (q.columns.empty()) {
            result.push_back(row);
        } else {
            Row projected;
            for (const auto& col : q.columns) {
                auto it = row.cols.find(col);
                if (it != row.cols.end()) projected.cols[col] = it->second;
            }
            result.push_back(projected);
        }
    }

    if (!q.order_by.empty()) {
        std::sort(result.begin(), result.end(), [&](const Row& a, const Row& b) {
            double va = row_val(a, q.order_by);
            double vb = row_val(b, q.order_by);
            return q.order_asc ? va < vb : va > vb;
        });
    }

    if (q.limit >= 0 && (int)result.size() > q.limit)
        result.resize(q.limit);

    return result;
}

void print_rows(const std::vector<Row>& rows,
                const std::vector<std::string>& col_order) {
    for (const auto& row : rows) {
        const std::vector<std::string>* keys = &col_order;
        std::vector<std::string> dynamic_keys;
        if (col_order.empty()) {
            for (const auto& kv : row.cols) dynamic_keys.push_back(kv.first);
            std::sort(dynamic_keys.begin(), dynamic_keys.end());
            keys = &dynamic_keys;
        }
        for (const auto& k : *keys) {
            auto it = row.cols.find(k);
            if (it == row.cols.end()) continue;
            std::cout << k << "=";
            if (it->second.type == ValType::NUM) std::cout << it->second.num;
            else                                  std::cout << it->second.str;
            std::cout << "  ";
        }
        std::cout << "\n";
    }
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int main() {
    shunting_demo();

    std::vector<Row> students = {
        {{{ "id", Value::from_num(1) }, { "name", Value::from_str("Alice") }, { "age", Value::from_num(22) }, { "gpa", Value::from_num(3.8) }}},
        {{{ "id", Value::from_num(2) }, { "name", Value::from_str("Bob")   }, { "age", Value::from_num(25) }, { "gpa", Value::from_num(2.9) }}},
        {{{ "id", Value::from_num(3) }, { "name", Value::from_str("Carol") }, { "age", Value::from_num(21) }, { "gpa", Value::from_num(3.5) }}},
        {{{ "id", Value::from_num(4) }, { "name", Value::from_str("Dave")  }, { "age", Value::from_num(30) }, { "gpa", Value::from_num(3.1) }}},
    };

    std::cout << "=== Part 2: Minimal SQL SELECT Parser ===\n\n";

    struct QueryDef {
        std::string              sql;
        std::vector<std::string> col_order;
    };

    QueryDef queries[] = {
        { "SELECT id, name, gpa FROM students WHERE gpa > 3.0 ORDER BY gpa DESC LIMIT 3",
          {"id", "name", "gpa"} },
        { "SELECT * FROM students WHERE age >= 22 && age <= 26",
          {"id", "name", "age", "gpa"} },
        { "SELECT name, age FROM students WHERE gpa >= 3.5 ORDER BY age ASC",
          {"name", "age"} },
    };

    for (const auto& qd : queries) {
        std::cout << "SQL: " << qd.sql << "\n";
        auto q   = parse_select(qd.sql);
        auto res = execute(q, students);
        if (res.empty())
            std::cout << "(no rows)\n";
        else
            print_rows(res, qd.col_order);
        std::cout << "\n";
    }

    return 0;
}
