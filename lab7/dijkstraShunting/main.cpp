// Lab 7 - Dijkstra Shunting-Yard evaluator for SQL WHERE clauses
// Jinesh Gandhi (24BCS10072) <jinesh.24bcs10072@sst.scaler.com>
//
// Tokenize an infix SQL-like WHERE condition, convert it to postfix (RPN)
// using Dijkstra's Shunting-Yard algorithm, and evaluate the resulting
// expression once per row against a vector<Employee>.
//
// Supported tokens:
//   identifiers (id, age, ...)   numeric literals
//   comparison: > < >= <= = !=
//   logical:    AND OR  (also && / || as aliases)
//   parens:     ( )

#include <algorithm>
#include <cctype>
#include <iostream>
#include <stack>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

struct Employee {
    std::string name;
    int id  = 0;
    int age = 0;
};

namespace {

struct OpInfo {
    int  precedence;
    bool right_assoc;
};

// Higher precedence binds tighter. Mirrors typical SQL evaluation order:
// comparisons bind tighter than AND, AND tighter than OR.
const std::unordered_map<std::string, OpInfo>& operatorTable() {
    static const std::unordered_map<std::string, OpInfo> kOps = {
        {"OR",  {1, false}},
        {"AND", {2, false}},
        {"=",   {3, false}}, {"!=", {3, false}},
        {"<",   {4, false}}, {">",  {4, false}},
        {"<=",  {4, false}}, {">=", {4, false}},
    };
    return kOps;
}

bool isOperator(const std::string& tok) {
    return operatorTable().find(tok) != operatorTable().end();
}

int precedence(const std::string& tok) {
    auto it = operatorTable().find(tok);
    return it == operatorTable().end() ? 0 : it->second.precedence;
}

bool isIntegerLiteral(const std::string& s) {
    if (s.empty()) return false;
    size_t i = (s[0] == '-' && s.size() > 1) ? 1 : 0;
    return std::all_of(s.begin() + i, s.end(),
                       [](unsigned char c) { return std::isdigit(c); });
}

std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return s;
}

}  // namespace

std::vector<std::string> tokenize(const std::string& query) {
    std::vector<std::string> tokens;
    size_t i = 0;
    const size_t n = query.size();

    while (i < n) {
        unsigned char c = static_cast<unsigned char>(query[i]);
        if (std::isspace(c)) { ++i; continue; }

        if (std::isalpha(c) || c == '_') {
            std::string lex;
            while (i < n && (std::isalnum(static_cast<unsigned char>(query[i])) ||
                             query[i] == '_')) {
                lex += query[i++];
            }
            std::string up = toUpper(lex);
            if (up == "AND" || up == "OR") tokens.push_back(up);
            else                            tokens.push_back(lex);
            continue;
        }

        if (std::isdigit(c)) {
            std::string lex;
            while (i < n && std::isdigit(static_cast<unsigned char>(query[i]))) {
                lex += query[i++];
            }
            tokens.push_back(std::move(lex));
            continue;
        }

        if ((query[i] == '>' || query[i] == '<' || query[i] == '!') &&
            i + 1 < n && query[i + 1] == '=') {
            tokens.push_back({query[i], query[i + 1]});
            i += 2;
            continue;
        }

        if (query[i] == '&' && i + 1 < n && query[i + 1] == '&') {
            tokens.push_back("AND"); i += 2; continue;
        }
        if (query[i] == '|' && i + 1 < n && query[i + 1] == '|') {
            tokens.push_back("OR"); i += 2; continue;
        }

        // Single-char operator or paren
        tokens.push_back(std::string(1, query[i]));
        ++i;
    }
    return tokens;
}

std::vector<std::string> toPostfix(const std::vector<std::string>& tokens) {
    std::vector<std::string> out;
    std::stack<std::string> ops;

    for (const auto& tok : tokens) {
        if (tok == "(") {
            ops.push(tok);
        } else if (tok == ")") {
            while (!ops.empty() && ops.top() != "(") {
                out.push_back(ops.top());
                ops.pop();
            }
            if (ops.empty()) throw std::runtime_error("Mismatched parentheses: missing '('");
            ops.pop();
        } else if (isOperator(tok)) {
            const auto& info = operatorTable().at(tok);
            while (!ops.empty() && ops.top() != "(") {
                int topPrec = precedence(ops.top());
                if (topPrec > info.precedence ||
                    (topPrec == info.precedence && !info.right_assoc)) {
                    out.push_back(ops.top());
                    ops.pop();
                } else break;
            }
            ops.push(tok);
        } else {
            out.push_back(tok);                   // identifier or literal
        }
    }
    while (!ops.empty()) {
        if (ops.top() == "(") throw std::runtime_error("Mismatched parentheses: missing ')'");
        out.push_back(ops.top());
        ops.pop();
    }
    return out;
}

int resolveField(const std::string& field, const Employee& e) {
    if (field == "id")  return e.id;
    if (field == "age") return e.age;
    throw std::runtime_error("Unknown identifier/column: '" + field + "'");
}

bool evaluatePostfix(const std::vector<std::string>& postfix, const Employee& e) {
    std::stack<int> st;
    for (const auto& tok : postfix) {
        if (!isOperator(tok)) {
            st.push(isIntegerLiteral(tok) ? std::stoi(tok) : resolveField(tok, e));
            continue;
        }
        if (st.size() < 2) {
            throw std::runtime_error("Malformed expression: not enough operands for '" + tok + "'");
        }
        int rhs = st.top(); st.pop();
        int lhs = st.top(); st.pop();

        if      (tok == ">")  st.push(lhs >  rhs);
        else if (tok == "<")  st.push(lhs <  rhs);
        else if (tok == ">=") st.push(lhs >= rhs);
        else if (tok == "<=") st.push(lhs <= rhs);
        else if (tok == "=")  st.push(lhs == rhs);
        else if (tok == "!=") st.push(lhs != rhs);
        else if (tok == "AND") st.push(lhs && rhs);
        else if (tok == "OR")  st.push(lhs || rhs);
        else throw std::runtime_error("Unsupported operator: '" + tok + "'");
    }
    if (st.size() != 1) {
        throw std::runtime_error("Malformed expression: stack does not collapse to a single value");
    }
    return st.top() != 0;
}

int main() {
    try {
        const std::string query = "id > 3 AND (age < 25 OR age >= 30)";

        auto tokens  = tokenize(query);
        auto postfix = toPostfix(tokens);

        std::cout << "Query:   " << query << '\n';
        std::cout << "Postfix:";
        for (const auto& t : postfix) std::cout << ' ' << t;
        std::cout << "\n\nMatching rows:\n";

        const std::vector<Employee> employees = {
            {"Pranav", 1, 19}, {"Aarav", 2, 20},  {"Karan", 3, 19},
            {"Sneha",         4, 21}, {"Vivaan", 5, 20}, {"Ishaan", 6, 31},
            {"Meera",         7, 22}, {"Devansh", 8, 33},
        };

        for (const auto& e : employees) {
            if (evaluatePostfix(postfix, e)) {
                std::cout << "  id=" << e.id << "  age=" << e.age << "  name=" << e.name << '\n';
            }
        }
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        return 1;
    }
    return 0;
}