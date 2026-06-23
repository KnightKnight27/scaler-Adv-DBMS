// Lab 7: Expression evaluator (Shunting-Yard) + basic SQL SELECT parser. 24BCS10005
// Compile: g++ -std=c++17 -o sql_parser sql_parser.cpp && ./sql_parser

#include <iostream>
#include <sstream>
#include <stack>
#include <vector>
#include <string>
#include <unordered_map>
#include <variant>
#include <algorithm>
#include <cctype>

// ---------- Operator helpers ----------

int op_rank(const std::string& tok) {
    if (tok == "||") return 1;
    if (tok == "&&") return 2;
    if (tok == "=" || tok == "!=") return 3;
    if (tok == "<" || tok == ">" || tok == "<=" || tok == ">=") return 4;
    if (tok == "+" || tok == "-") return 5;
    if (tok == "*" || tok == "/") return 6;
    return 0;
}

bool is_operator(const std::string& tok) {
    return op_rank(tok) > 0;
}

// ---------- Tokenizer ----------

std::vector<std::string> scan(const std::string& text) {
    std::vector<std::string> parts;
    size_t pos = 0;
    while (pos < text.size()) {
        char ch = text[pos];
        if (std::isspace(static_cast<unsigned char>(ch))) {
            ++pos;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(ch)) || ch == '.') {
            size_t end = pos;
            while (end < text.size() && (std::isdigit(static_cast<unsigned char>(text[end])) || text[end] == '.'))
                ++end;
            parts.push_back(text.substr(pos, end - pos));
            pos = end;
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '_') {
            size_t end = pos;
            while (end < text.size() && (std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_'))
                ++end;
            parts.push_back(text.substr(pos, end - pos));
            pos = end;
            continue;
        }
        if (ch == '(' || ch == ')') {
            parts.push_back(std::string(1, ch));
            ++pos;
            continue;
        }
        // Multi-character operators
        std::string maybe = text.substr(pos, 2);
        if (maybe == "!=" || maybe == "<=" || maybe == ">=" || maybe == "&&" || maybe == "||") {
            parts.push_back(maybe);
            pos += 2;
            continue;
        }
        parts.push_back(std::string(1, ch));
        ++pos;
    }
    return parts;
}

// ---------- Infix → RPN (Shunting-Yard) ----------

std::vector<std::string> to_postfix(const std::vector<std::string>& tokens) {
    std::vector<std::string> output;
    std::stack<std::string> op_stack;
    for (const auto& t : tokens) {
        if (t == "(") {
            op_stack.push(t);
        } else if (t == ")") {
            while (!op_stack.empty() && op_stack.top() != "(") {
                output.push_back(op_stack.top());
                op_stack.pop();
            }
            if (!op_stack.empty()) op_stack.pop();
        } else if (is_operator(t)) {
            while (!op_stack.empty() && is_operator(op_stack.top()) && op_rank(op_stack.top()) >= op_rank(t)) {
                output.push_back(op_stack.top());
                op_stack.pop();
            }
            op_stack.push(t);
        } else {
            output.push_back(t);
        }
    }
    while (!op_stack.empty()) {
        output.push_back(op_stack.top());
        op_stack.pop();
    }
    return output;
}

// ---------- RPN evaluator ----------

double evaluate_postfix(const std::vector<std::string>& rpn,
                        const std::unordered_map<std::string, double>& bindings) {
    std::stack<double> st;
    for (const auto& tok : rpn) {
        if (is_operator(tok)) {
            double b = st.top(); st.pop();
            double a = st.top(); st.pop();
            if      (tok == "+")  st.push(a + b);
            else if (tok == "-")  st.push(a - b);
            else if (tok == "*")  st.push(a * b);
            else if (tok == "/")  st.push(a / b);
            else if (tok == "<")  st.push(a < b);
            else if (tok == ">")  st.push(a > b);
            else if (tok == "<=") st.push(a <= b);
            else if (tok == ">=") st.push(a >= b);
            else if (tok == "=")  st.push(a == b);
            else if (tok == "!=") st.push(a != b);
            else if (tok == "&&") st.push(a && b);
            else if (tok == "||") st.push(a || b);
        } else if (std::isdigit(static_cast<unsigned char>(tok[0])) || tok[0] == '.') {
            st.push(std::stod(tok));
        } else {
            auto it = bindings.find(tok);
            st.push(it == bindings.end() ? 0.0 : it->second);
        }
    }
    return st.top();
}

void demo_shunting() {
    std::string expr = "age * 2 + salary / 1000 > 100";
    auto rpn = to_postfix(scan(expr));

    std::cout << "Expression : " << expr << "\n";
    std::cout << "Postfix    : ";
    for (auto& t : rpn) std::cout << t << " ";
    std::cout << "\n";

    std::unordered_map<std::string, double> bindings = {{"age", 30}, {"salary", 50000}};
    std::cout << "Result     : " << (evaluate_postfix(rpn, bindings) ? "true" : "false") << "\n\n";
}

// ---------- Part 2: SQL SELECT over in-memory rows ----------

using Field = std::variant<double, std::string>;

struct Record {
    std::unordered_map<std::string, Field> fields;
};

double get_numeric(const Record& rec, const std::string& col) {
    auto it = rec.fields.find(col);
    if (it == rec.fields.end()) return 0.0;
    if (auto* v = std::get_if<double>(&it->second)) return *v;
    return 0.0;
}

struct ParsedQuery {
    std::vector<std::string> cols;    // empty = SELECT *
    std::string table;
    std::string condition;
    std::string sort_col;
    bool sort_asc = true;
    int max_rows = -1;
};

std::string to_upper(std::string s) {
    for (auto& c : s) c = std::toupper(static_cast<unsigned char>(c));
    return s;
}

ParsedQuery parse_sql(const std::string& sql) {
    ParsedQuery q;
    std::istringstream in(sql);
    std::string word;

    in >> word;

    while (in >> word && to_upper(word) != "FROM") {
        if (!word.empty() && word.back() == ',') word.pop_back();
        if (word != "*") q.cols.push_back(word);
    }

    in >> q.table;

    while (in >> word) {
        std::string kw = to_upper(word);
        if (kw == "WHERE") {
            bool done = false;
            while (in >> word) {
                kw = to_upper(word);
                if (kw == "ORDER" || kw == "LIMIT") { done = true; break; }
                if (!q.condition.empty()) q.condition += " ";
                q.condition += word;
            }
            if (done && kw == "ORDER") {
                in >> word;
                in >> q.sort_col;
                if (in >> word && to_upper(word) == "DESC") q.sort_asc = false;
            }
            if (done && kw == "LIMIT") {
                in >> q.max_rows;
            }
            continue;
        }
        if (kw == "ORDER") {
            in >> word;
            in >> q.sort_col;
            if (in >> word && to_upper(word) == "DESC") q.sort_asc = false;
        }
        if (kw == "LIMIT") {
            in >> q.max_rows;
        }
    }
    return q;
}

std::vector<Record> run_query(const ParsedQuery& q, const std::vector<Record>& dataset) {
    std::vector<std::string> postfix;
    if (!q.condition.empty()) postfix = to_postfix(scan(q.condition));

    std::vector<Record> matched;
    for (const auto& rec : dataset) {
        if (!postfix.empty()) {
            std::unordered_map<std::string, double> vals;
            for (auto& [k, v] : rec.fields) vals[k] = get_numeric(rec, k);
            if (!evaluate_postfix(postfix, vals)) continue;
        }
        if (q.cols.empty()) {
            matched.push_back(rec);
        } else {
            Record projected;
            for (auto& c : q.cols)
                if (rec.fields.count(c)) projected.fields[c] = rec.fields.at(c);
            matched.push_back(projected);
        }
    }

    if (!q.sort_col.empty()) {
        std::sort(matched.begin(), matched.end(), [&](const Record& a, const Record& b) {
            double va = get_numeric(a, q.sort_col);
            double vb = get_numeric(b, q.sort_col);
            return q.sort_asc ? va < vb : va > vb;
        });
    }

    if (q.max_rows >= 0 && static_cast<int>(matched.size()) > q.max_rows)
        matched.resize(q.max_rows);

    return matched;
}

void display(const std::vector<Record>& rows) {
    for (const auto& rec : rows) {
        for (const auto& [k, v] : rec.fields) {
            std::cout << k << "=";
            if (auto* d = std::get_if<double>(&v)) std::cout << *d;
            else                                   std::cout << std::get<std::string>(v);
            std::cout << "  ";
        }
        std::cout << "\n";
    }
}

// ---------- Main ----------

int main() {
    demo_shunting();

    std::vector<Record> dataset = {
        {{{"id", 1.0}, {"name", std::string("Alice")}, {"age", 22.0}, {"gpa", 3.8}}},
        {{{"id", 2.0}, {"name", std::string("Bob")},   {"age", 25.0}, {"gpa", 2.9}}},
        {{{"id", 3.0}, {"name", std::string("Carol")}, {"age", 21.0}, {"gpa", 3.5}}},
        {{{"id", 4.0}, {"name", std::string("Dave")},  {"age", 30.0}, {"gpa", 3.1}}},
    };

    std::vector<std::string> test_cases = {
        "SELECT id, name, gpa FROM students WHERE gpa > 3.0 ORDER BY gpa DESC LIMIT 3",
        "SELECT * FROM students WHERE age >= 22 && age <= 26",
    };

    for (const auto& sql : test_cases) {
        std::cout << "SQL: " << sql << "\n";
        display(run_query(parse_sql(sql), dataset));
        std::cout << "\n";
    }
}
