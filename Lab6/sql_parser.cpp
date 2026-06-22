#include <iostream>
#include <vector>
#include <unordered_map>
#include <variant>
#include <sstream>
#include <stack>
#include <algorithm>
#include <iomanip>
#include <cctype>
#include <cmath>
#include <stdexcept>

// Using std::variant for multi-type fields
using Value = std::variant<int, double, std::string>;

struct Record {
    std::unordered_map<std::string, Value> fields;
};

struct Table {
    std::string name;
    std::vector<Record> rows;
};

// --- Operator Metadata for Complex WHERE clauses ---
struct OpInfo { int precedence; bool right_assoc; };
const std::unordered_map<std::string, OpInfo> OPS = {
    {"||", {1, false}}, {"&&", {2, false}},
    {"=",  {3, false}}, {"!=", {3, false}},
    {"<",  {4, false}}, {">",  {4, false}}, {"<=", {4, false}}, {">=", {4, false}},
    {"+",  {5, false}}, {"-",  {5, false}},
    {"*",  {6, false}}, {"/",  {6, false}}
};

// Helper to convert Variant or Column to a numerical evaluation context
double getAsDouble(const Value& val) {
    if (std::holds_alternative<int>(val)) return std::get<int>(val);
    if (std::holds_alternative<double>(val)) return std::get<double>(val);
    try { return std::stod(std::get<std::string>(val)); } catch (...) {}
    return 0.0;
}

struct Query {
    std::vector<std::string> columns; // Holds specific columns to select, empty means '*'
    std::string whereRaw;             // Holds the unparsed math/logical string
    std::vector<std::string> whereRPN;// Evaluatable RPN sequence
    std::string orderBy;
    bool desc = false;
    int limit = -1;
};

class SQLParser {
private:
    std::string toUpper(std::string s) {
        for (auto& c : s) c = std::toupper((unsigned char)c);
        return s;
    }

    std::vector<std::string> tokenize(const std::string& expr) {
        std::vector<std::string> tokens;
        int i = 0, n = expr.size();
        while (i < n) {
            if (std::isspace((unsigned char)expr[i])) { i++; continue; }
            if (std::isdigit((unsigned char)expr[i]) || (expr[i] == '.' && i + 1 < n && std::isdigit((unsigned char)expr[i + 1]))) {
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
                if (i + 1 < n) {
                    std::string two = expr.substr(i, 2);
                    if (OPS.count(two)) { tokens.push_back(two); i += 2; continue; }
                }
                tokens.push_back(std::string(1, expr[i++]));
            }
        }
        return tokens;
    }

    std::vector<std::string> toRPN(const std::vector<std::string>& tokens) {
        std::vector<std::string> output;
        std::stack<std::string> ops;
        for (const auto& tok : tokens) {
            if (tok == "(") ops.push(tok);
            else if (tok == ")") {
                while (!ops.empty() && ops.top() != "(") { output.push_back(ops.top()); ops.pop(); }
                if (ops.empty()) throw std::runtime_error("Mismatched parentheses");
                ops.pop();
            } else if (OPS.count(tok)) {
                const auto& o1 = OPS.at(tok);
                while (!ops.empty() && OPS.count(ops.top())) {
                    const auto& o2 = OPS.at(ops.top());
                    if (o2.precedence > o1.precedence || (o2.precedence == o1.precedence && !o1.right_assoc)) {
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

public:
    Query parse(const std::string& sql) {
        Query q;
        std::istringstream ss(sql);
        std::string word;
        ss >> word; // Consume SELECT

        // Parse Target Columns
        while (ss >> word && toUpper(word) != "FROM") {
            if (!word.empty() && word.back() == ',') word.pop_back();
            if (word == "*") q.columns.clear();
            else q.columns.push_back(word);
        }

        std::string tableName;
        ss >> tableName; // Read table name

        // Parse contextual trailing statements
        while (ss >> word) {
            std::string kw = toUpper(word);
            if (kw == "WHERE") {
                std::string clause, w2;
                while (ss >> w2) {
                    if (toUpper(w2) == "ORDER" || toUpper(w2) == "LIMIT") {
                        word = w2; goto process_where;
                    }
                    clause += (clause.empty() ? "" : " ") + w2;
                }
                word = "";
            process_where:
                q.whereRaw = clause;
                q.whereRPN = toRPN(tokenize(q.whereRaw));
                if (word.empty()) break;
                kw = toUpper(word);
            }
            if (kw == "ORDER") {
                ss >> word; // Consume BY
                ss >> q.orderBy;
                std::string dir;
                if (ss >> dir && toUpper(dir) == "DESC") q.desc = true;
            }
            if (kw == "LIMIT") {
                ss >> q.limit;
            }
        }
        return q;
    }
};

class Executor {
private:
    double evalRPN(const std::vector<std::string>& rpn, const Record& r) {
        std::stack<double> stk;
        for (const auto& tok : rpn) {
            if (OPS.count(tok)) {
                if(stk.size() < 2) return 0.0;
                double b = stk.top(); stk.pop();
                double a = stk.top(); stk.pop();
                if      (tok == "+")  stk.push(a + b);
                else if (tok == "-")  stk.push(a - b);
                else if (tok == "*")  stk.push(a * b);
                else if (tok == "/")  stk.push(b != 0 ? a / b : 0);
                else if (tok == "<")  stk.push(a < b ? 1.0 : 0.0);
                else if (tok == ">")  stk.push(a > b ? 1.0 : 0.0);
                else if (tok == "<=") stk.push(a <= b ? 1.0 : 0.0);
                else if (tok == ">=") stk.push(a >= b ? 1.0 : 0.0);
                else if (tok == "=")  stk.push(a == b ? 1.0 : 0.0);
                else if (tok == "!=") stk.push(a != b ? 1.0 : 0.0);
                else if (tok == "&&") stk.push((a != 0.0 && b != 0.0) ? 1.0 : 0.0);
                else if (tok == "||") stk.push((a != 0.0 || b != 0.0) ? 1.0 : 0.0);
            } else {
                try { stk.push(std::stod(tok)); }
                catch (...) {
                    auto it = r.fields.find(tok);
                    if (it != r.fields.end()) stk.push(getAsDouble(it->second));
                    else stk.push(0.0); // Unknown variables evaluate to 0 fallback
                }
            }
        }
        return stk.empty() ? 0.0 : stk.top();
    }

public:
    std::vector<Record> run(const Query& q, const Table& t) {
        std::vector<Record> result;

        for (auto &r : t.rows) {
            bool ok = true;
            if (!q.whereRPN.empty()) {
                ok = (evalRPN(q.whereRPN, r) != 0.0);
            }

            if (ok) {
                // Handle column projections (filtering fields if specific columns were targeted)
                if (q.columns.empty()) {
                    result.push_back(r);
                } else {
                    Record projected;
                    for (const auto& col : q.columns) {
                        if (r.fields.count(col)) projected.fields[col] = r.fields.at(col);
                    }
                    result.push_back(projected);
                }
            }
        }

        // Sorting Segment
        if (!q.orderBy.empty()) {
            std::sort(result.begin(), result.end(), [&](const Record& a, const Record& b) {
                double va = (a.fields.count(q.orderBy)) ? getAsDouble(a.fields.at(q.orderBy)) : 0.0;
                double vb = (b.fields.count(q.orderBy)) ? getAsDouble(b.fields.at(q.orderBy)) : 0.0;
                return q.desc ? va > vb : va < vb;
            });
        }

        // Apply Limit
        if (q.limit != -1 && (int)result.size() > q.limit)
            result.resize(q.limit);

        return result;
    }
};

// Adaptive output writer that checks dynamic available fields safely
void printRows(const std::vector<Record>& rows) {
    if(rows.empty()) { std::cout << "(No records found)\n"; return; }

    // Print headers dynamically depending on what keys are present
    std::vector<std::string> keys;
    for(const auto& pair : rows[0].fields) keys.push_back(pair.first);
    std::sort(keys.begin(), keys.end()); // deterministic column ordering

    for(const auto& key : keys) {
        std::string upperKey = key;
        for(auto &c: upperKey) c = std::toupper(c);
        std::cout << std::left << std::setw(12) << upperKey;
    }
    std::cout << "\n" << std::string(keys.size() * 12, '-') << "\n";

    for (auto& r : rows) {
        for(const auto& key : keys) {
            auto val = r.fields.at(key);
            if (std::holds_alternative<int>(val)) std::cout << std::left << std::setw(12) << std::get<int>(val);
            else if (std::holds_alternative<double>(val)) std::cout << std::left << std::setw(12) << std::get<double>(val);
            else std::cout << std::left << std::setw(12) << std::get<std::string>(val);
        }
        std::cout << '\n';
    }
}

int main() {
    Table students{"students"};
    students.rows = {
        {{{"id", 1}, {"name", "Alice"}, {"age", 21}, {"gpa", 3.8}}},
        {{{"id", 2}, {"name", "Bob"},   {"age", 19}, {"gpa", 3.1}}},
        {{{"id", 3}, {"name", "Carol"}, {"age", 23}, {"gpa", 3.9}}},
        {{{"id", 4}, {"name", "David"}, {"age", 20}, {"gpa", 2.9}}},
        {{{"id", 5}, {"name", "Emma"},  {"age", 24}, {"gpa", 3.6}}}
    };

    std::vector<std::string> testQueries = {
        "SELECT * FROM students WHERE gpa >= 3.5 ORDER BY gpa DESC LIMIT 3",
        "SELECT id, name FROM students WHERE age >= 20 && age <= 23 ORDER BY age ASC",
        "SELECT name, gpa FROM students WHERE gpa * 10 > 35"
    };

    SQLParser parser;
    Executor exec;

    for (const auto& sql : testQueries) {
        std::cout << "\nExecuting Query: " << sql << "\n";
        Query q = parser.parse(sql);
        auto result = exec.run(q, students);
        printRows(result);
        std::cout << "Rows Returned: " << result.size() << "\n";
    }
}