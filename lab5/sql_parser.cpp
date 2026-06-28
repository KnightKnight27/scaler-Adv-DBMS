// Lab 5: Shunting-Yard Algorithm + Minimal SQL SELECT Parser
// Compile: g++ -std=c++17 -o sql_parser sql_parser.cpp
// Run:     ./sql_parser
//
// Two parts:
//   Part 1: Tokenize + Shunting-Yard + RPN evaluation (WHERE clause evaluator)
//   Part 2: parse_select() → execute() over std::vector<Row>

#include <iostream>
#include <sstream>
#include <stack>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <stdexcept>
#include <cctype>
#include <functional>
#include <iomanip>
#include <cstdlib>
#include <cmath>

// ════════════════════════════════════════════════════════════════════
// PART 1: Shunting-Yard + RPN evaluator
// ════════════════════════════════════════════════════════════════════

struct OpInfo { int precedence; bool right_assoc; };

static const std::unordered_map<std::string, OpInfo> OPS = {
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

// ── Tokenizer ──
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
        } else if (expr[i] == '\'' || expr[i] == '"') {
            char quote = expr[i];
            int j = i + 1;
            while (j < n && expr[j] != quote) j++;
            if (j >= n) throw std::runtime_error("Unterminated string literal");
            tokens.push_back(expr.substr(i, j - i + 1));   // include quotes as marker
            i = j + 1;
        } else if (expr[i] == '(' || expr[i] == ')') {
            tokens.emplace_back(1, expr[i++]);
        } else {
            if (i + 1 < n) {
                std::string two = expr.substr(i, 2);
                if (OPS.count(two)) { tokens.push_back(two); i += 2; continue; }
            }
            tokens.emplace_back(1, expr[i++]);
        }
    }
    return tokens;
}

// ── Shunting-Yard: infix → postfix ──
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

// ── Evaluate RPN with variable map (numbers + string literals) ──
// Uses a tagged stack: each value is a pair {is_string, value}.
struct TaggedValue {
    bool        is_string = false;
    double      num = 0.0;
    std::string str;
    TaggedValue() = default;
    TaggedValue(double d)        : is_string(false), num(d) {}
    TaggedValue(std::string s)   : is_string(true),  num(0.0), str(std::move(s)) {}
};

double eval_rpn(const std::vector<std::string>& rpn,
                const std::unordered_map<std::string, double>& vars,
                const std::unordered_map<std::string, std::string>& str_vars) {
    std::stack<TaggedValue> stk;
    auto pop = [&]() { TaggedValue v = stk.top(); stk.pop(); return v; };

    for (const auto& tok : rpn) {
        if (OPS.count(tok)) {
            TaggedValue b = pop();
            TaggedValue a = pop();

            // String semantics if either operand is a string
            if (a.is_string || b.is_string) {
                std::string sa = a.is_string ? a.str : std::to_string(a.num);
                std::string sb = b.is_string ? b.str : std::to_string(b.num);
                if      (tok == "=")  stk.push(TaggedValue((sa == sb) ? 1.0 : 0.0));
                else if (tok == "!=") stk.push(TaggedValue((sa != sb) ? 1.0 : 0.0));
                else if (tok == "<")  stk.push(TaggedValue((sa <  sb) ? 1.0 : 0.0));
                else if (tok == ">")  stk.push(TaggedValue((sa >  sb) ? 1.0 : 0.0));
                else if (tok == "<=") stk.push(TaggedValue((sa <= sb) ? 1.0 : 0.0));
                else if (tok == ">=") stk.push(TaggedValue((sa >= sb) ? 1.0 : 0.0));
                else throw std::runtime_error("Operator " + tok + " not supported on strings");
            } else {
                double da = a.num, db = b.num;
                if      (tok == "+")  stk.push(TaggedValue(da + db));
                else if (tok == "-")  stk.push(TaggedValue(da - db));
                else if (tok == "*")  stk.push(TaggedValue(da * db));
                else if (tok == "/")  stk.push(TaggedValue(da / db));
                else if (tok == "^")  stk.push(TaggedValue(std::pow(da, db)));
                else if (tok == "<")  stk.push(TaggedValue((da <  db) ? 1.0 : 0.0));
                else if (tok == ">")  stk.push(TaggedValue((da >  db) ? 1.0 : 0.0));
                else if (tok == "<=") stk.push(TaggedValue((da <= db) ? 1.0 : 0.0));
                else if (tok == ">=") stk.push(TaggedValue((da >= db) ? 1.0 : 0.0));
                else if (tok == "=")  stk.push(TaggedValue((da == db) ? 1.0 : 0.0));
                else if (tok == "!=") stk.push(TaggedValue((da != db) ? 1.0 : 0.0));
                else if (tok == "&&") stk.push(TaggedValue((da && db) ? 1.0 : 0.0));
                else if (tok == "||") stk.push(TaggedValue((da || db) ? 1.0 : 0.0));
            }
        } else if (tok.size() >= 2 &&
                   (tok.front() == '\'' || tok.front() == '"')) {
            // String literal — strip quotes
            stk.push(TaggedValue(tok.substr(1, tok.size() - 2)));
        } else {
            try { stk.push(TaggedValue(std::stod(tok))); }
            catch (...) {
                auto it = str_vars.find(tok);
                if (it != str_vars.end()) {
                    stk.push(TaggedValue(it->second));
                    continue;
                }
                auto it2 = vars.find(tok);
                if (it2 == vars.end())
                    throw std::runtime_error("Unknown variable: " + tok);
                stk.push(TaggedValue(it2->second));
            }
        }
    }
    TaggedValue result = stk.top();
    return result.is_string ? (result.str.empty() ? 0.0 : 1.0) : result.num;
}

void shunting_demo() {
    std::cout << "=== PART 1: Shunting-Yard ===\n\n";

    struct TestCase {
        std::string expr;
        std::unordered_map<std::string, double> vars;
        double expected;
        const char* desc;
    };
    std::vector<TestCase> tests = {
        {"age * 2 + salary / 1000 > 100", {{"age", 30}, {"salary", 50000}}, 1.0,  "compound expression"},
        {"(2 + 3) * 4",                  {{}},                              20.0, "parens force precedence"},
        {"2 + 3 * 4",                    {{}},                              14.0, "precedence"},
        {"2 ^ 3 ^ 2",                    {{}},                              512.0,"right-assoc: 2^(3^2)=512"},
        {"(2 ^ 3) ^ 2",                  {{}},                              64.0, "with parens: 8^2=64"},
        {"x && y || z",                  {{"x", 0}, {"y", 1}, {"z", 1}},    1.0,  "AND higher than OR"},
        {"x && y || z",                  {{"x", 0}, {"y", 1}, {"z", 0}},    0.0,  "AND higher than OR (z=0)"},
        {"a >= 10 && a <= 20",           {{"a", 15}},                       1.0,  "range check"},
    };

    for (const auto& t : tests) {
        std::cout << "Expression: " << t.expr << "  (" << t.desc << ")\n";
        auto toks = tokenize(t.expr);
        auto rpn  = to_rpn(toks);
        double result = eval_rpn(rpn, t.vars, {});

        std::cout << "  Tokens : ";
        for (auto& t2 : toks) std::cout << t2 << " ";
        std::cout << "\n  RPN    : ";
        for (auto& t2 : rpn) std::cout << t2 << " ";
        std::cout << "\n  Result : " << result << " (expected " << t.expected << ") "
                  << (std::abs(result - t.expected) < 1e-9 ? "OK" : "FAIL")
                  << "\n\n";
    }

    // ── String-aware tests ──
    std::unordered_map<std::string, double>      empty_num;
    std::unordered_map<std::string, std::string> svars = {{"name", "Alice"}};
    auto rt = tokenize("name = 'Alice'");
    auto rp = to_rpn(rt);
    double res_eq = eval_rpn(rp, empty_num, svars);
    std::cout << "name = 'Alice'  -> " << res_eq << " (expected 1) "
              << (res_eq == 1.0 ? "OK" : "FAIL") << "\n";

    auto rt2 = tokenize("name = 'Bob'");
    auto rp2 = to_rpn(rt2);
    double res_neq = eval_rpn(rp2, empty_num, svars);
    std::cout << "name = 'Bob'    -> " << res_neq << " (expected 0) "
              << (res_neq == 0.0 ? "OK" : "FAIL") << "\n\n";
}

// ════════════════════════════════════════════════════════════════════
// PART 2: Minimal SQL SELECT parser
// (Avoids <variant> from C++17 — uses a small tagged struct)
// ════════════════════════════════════════════════════════════════════

enum class ValueType { DOUBLE, STRING };

struct Value {
    ValueType tag;
    double    num;
    std::string str;

    Value()                : tag(ValueType::DOUBLE), num(0.0), str() {}
    Value(double d)        : tag(ValueType::DOUBLE), num(d),   str() {}
    Value(std::string s)   : tag(ValueType::STRING), num(0.0), str(s) {}
};

struct Row {
    std::unordered_map<std::string, Value> cols;
};

double row_val(const Row& row, const std::string& col) {
    auto it = row.cols.find(col);
    if (it == row.cols.end()) return 0.0;
    if (it->second.tag == ValueType::DOUBLE) return it->second.num;
    try { return std::stod(it->second.str); } catch (...) { return 0.0; }
}

std::string value_to_string(const Value& v) {
    if (v.tag == ValueType::DOUBLE) {
        std::ostringstream os; os << v.num; return os.str();
    }
    return v.str;
}

struct SelectQuery {
    std::vector<std::string>  columns;   // empty = SELECT *
    std::string               from;
    std::string               where_raw;
    std::string               order_by;
    bool                      order_asc = true;
    int                       limit = -1;
};

static std::string to_upper(std::string s) {
    for (auto& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

SelectQuery parse_select(const std::string& sql) {
    SelectQuery q;
    std::istringstream ss(sql);
    std::string word;
    ss >> word; // SELECT

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
                    word = w2;
                    goto apply_where;
                }
                clause += (clause.empty() ? "" : " ") + w2;
            }
            q.where_raw = clause;
            break;
        apply_where:
            q.where_raw = clause;
            kw = to_upper(word);
        }
        if (kw == "ORDER") {
            ss >> word; // BY
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

std::vector<Row> execute(const SelectQuery& q, const std::vector<Row>& data) {
    std::vector<std::string> rpn;
    if (!q.where_raw.empty())
        rpn = to_rpn(tokenize(q.where_raw));

    std::vector<Row> result;

    for (const auto& row : data) {
        if (!rpn.empty()) {
            std::unordered_map<std::string, double>      vars;
            std::unordered_map<std::string, std::string> svars;
            for (auto& kv : row.cols) {
                if (kv.second.tag == ValueType::DOUBLE) {
                    vars[kv.first] = kv.second.num;
                } else {
                    vars[kv.first] = row_val(row, kv.first);   // numeric fallback
                    svars[kv.first] = kv.second.str;
                }
            }
            if (eval_rpn(rpn, vars, svars) == 0.0) continue;
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
            double va = row_val(a, q.order_by);
            double vb = row_val(b, q.order_by);
            return q.order_asc ? va < vb : va > vb;
        });
    }

    if (q.limit >= 0 && (int)result.size() > q.limit)
        result.resize(q.limit);

    return result;
}

void print_rows(const std::vector<Row>& rows) {
    if (rows.empty()) { std::cout << "  (empty result set)\n"; return; }

    std::vector<std::string> cols;
    for (auto& kv : rows.front().cols) cols.push_back(kv.first);

    std::unordered_map<std::string, int> widths;
    for (auto& c : cols) widths[c] = (int)c.size();
    for (auto& row : rows)
        for (auto& c : cols) {
            std::string v = "NULL";
            auto it = row.cols.find(c);
            if (it != row.cols.end()) v = value_to_string(it->second);
            widths[c] = std::max(widths[c], (int)v.size());
        }

    for (auto& c : cols) std::cout << std::setw(widths[c] + 2) << std::left << c;
    std::cout << "\n";
    for (auto& c : cols) std::cout << std::string(widths[c] + 2, '-');
    std::cout << "\n";
    for (auto& row : rows) {
        for (auto& c : cols) {
            std::string v = "NULL";
            auto it = row.cols.find(c);
            if (it != row.cols.end()) v = value_to_string(it->second);
            std::cout << std::setw(widths[c] + 2) << std::left << v;
        }
        std::cout << "\n";
    }
}

// ════════════════════════════════════════════════════════════════════
// MAIN
// ════════════════════════════════════════════════════════════════════

int main() {
    shunting_demo();

    std::cout << "=== PART 2: SQL SELECT over vector<Row> ===\n\n";

    std::vector<Row> students = {
        { {{ "id", 1.0 }, { "name", std::string("Alice") }, { "age", 22.0 }, { "gpa", 3.8 }} },
        { {{ "id", 2.0 }, { "name", std::string("Bob")   }, { "age", 25.0 }, { "gpa", 2.9 }} },
        { {{ "id", 3.0 }, { "name", std::string("Carol") }, { "age", 21.0 }, { "gpa", 3.5 }} },
        { {{ "id", 4.0 }, { "name", std::string("Dave")  }, { "age", 30.0 }, { "gpa", 3.1 }} },
        { {{ "id", 5.0 }, { "name", std::string("Eve")   }, { "age", 28.0 }, { "gpa", 3.6 }} },
        { {{ "id", 6.0 }, { "name", std::string("Frank") }, { "age", 23.0 }, { "gpa", 2.7 }} },
        { {{ "id", 7.0 }, { "name", std::string("Grace") }, { "age", 24.0 }, { "gpa", 3.9 }} },
        { {{ "id", 8.0 }, { "name", std::string("Hank")  }, { "age", 27.0 }, { "gpa", 3.2 }} },
    };

    struct Q { const char* sql; const char* desc; };
    Q queries[] = {
        { "SELECT id, name, gpa FROM students WHERE gpa > 3.0 ORDER BY gpa DESC LIMIT 3", "gpa > 3.0, top 3 by gpa" },
        { "SELECT * FROM students WHERE age >= 22 && age <= 26",                          "age range 22-26"          },
        { "SELECT name, age FROM students WHERE gpa >= 3.5 ORDER BY age ASC",             "high gpa, ascending age"  },
        { "SELECT * FROM students WHERE name = 'Alice'",                                "equality on string col"   },
        { "SELECT * FROM students",                                                       "SELECT * (no filter)"     },
    };

    for (auto& q : queries) {
        std::cout << "SQL: " << q.sql << "  (" << q.desc << ")\n";
        try {
            auto parsed = parse_select(q.sql);
            auto res    = execute(parsed, students);
            print_rows(res);
        } catch (const std::exception& e) {
            std::cerr << "ERROR: " << e.what() << "\n";
        }
        std::cout << "\n";
    }

    return 0;
}