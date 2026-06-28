// Lab Session 5 — Shunting-Yard Algorithm + Minimal SQL SELECT Parser
// Student : Indrajeet Yadav | Roll No: 23BCS10199
//
// Part 1: Dijkstra's Shunting-Yard algorithm
//   Converts infix arithmetic/boolean expressions to postfix (RPN) and evaluates them.
//   This is the mechanism SQL engines use to evaluate WHERE clause expressions.
//
// Part 2: Minimal SQL SELECT parser + in-memory executor
//   Parses SELECT ... FROM ... WHERE ... ORDER BY ... LIMIT ...
//   Executes it against a vector<Row> (simulates a storage layer result).
//
// Build: g++ -std=c++17 -Wall -Wextra -O2 sql_parser.cpp -o sql_parser
// Run:   ./sql_parser

#include <iostream>
#include <sstream>
#include <stack>
#include <vector>
#include <string>
#include <unordered_map>
#include <variant>
#include <stdexcept>
#include <cctype>
#include <algorithm>
#include <functional>
#include <cassert>
#include <iomanip>
#include <cmath>

// =============================================================================
// PART 1: SHUNTING-YARD ALGORITHM
// =============================================================================

// ─────────────────────────────────────────────────────────────────────────────
// Operator metadata: precedence and associativity
// ─────────────────────────────────────────────────────────────────────────────

struct OpInfo {
    int  precedence;
    bool right_assoc;  // true for exponentiation; false for everything else
};

// Full operator table: covers all SQL WHERE expression operators
const std::unordered_map<std::string, OpInfo> OPERATORS = {
    // Logical operators (lowest precedence)
    {"OR",  {1, false}},  {"||", {1, false}},
    {"AND", {2, false}},  {"&&", {2, false}},
    {"NOT", {3, true }},  // unary, right-associative

    // Comparison operators
    {"=",   {4, false}},
    {"!=",  {4, false}},  {"<>", {4, false}},
    {"<",   {5, false}},
    {">",   {5, false}},
    {"<=",  {5, false}},
    {">=",  {5, false}},

    // Arithmetic operators
    {"+",   {6, false}},
    {"-",   {6, false}},
    {"*",   {7, false}},
    {"/",   {7, false}},
    {"%",   {7, false}},
    {"^",   {8, true }},  // exponentiation (right-associative)
};

// ─────────────────────────────────────────────────────────────────────────────
// Tokenizer
// ─────────────────────────────────────────────────────────────────────────────

enum class TokenType { NUMBER, STRING_LITERAL, IDENTIFIER, OPERATOR, LPAREN, RPAREN, COMMA };

struct Token {
    TokenType   type;
    std::string value;
};

std::vector<Token> tokenize(const std::string& expr) {
    std::vector<Token> tokens;
    int i = 0, n = (int)expr.size();

    while (i < n) {
        // Skip whitespace
        if (std::isspace(expr[i])) { i++; continue; }

        // Numeric literal (integer or decimal)
        if (std::isdigit(expr[i]) ||
            (expr[i] == '.' && i+1 < n && std::isdigit(expr[i+1]))) {
            int j = i;
            while (j < n && (std::isdigit(expr[j]) || expr[j] == '.')) j++;
            tokens.push_back({TokenType::NUMBER, expr.substr(i, j - i)});
            i = j;
            continue;
        }

        // String literal: 'value'
        if (expr[i] == '\'') {
            int j = i + 1;
            while (j < n && expr[j] != '\'') j++;
            tokens.push_back({TokenType::STRING_LITERAL, expr.substr(i+1, j-i-1)});
            i = j + 1;
            continue;
        }

        // Identifier or keyword (SQL keywords like AND, OR, NOT are operators)
        if (std::isalpha(expr[i]) || expr[i] == '_') {
            int j = i;
            while (j < n && (std::isalnum(expr[j]) || expr[j] == '_')) j++;
            std::string word = expr.substr(i, j - i);
            // Uppercase for keyword matching
            std::string upper = word;
            for (auto& c : upper) c = std::toupper(c);
            if (OPERATORS.count(upper))
                tokens.push_back({TokenType::OPERATOR, upper});
            else
                tokens.push_back({TokenType::IDENTIFIER, word});
            i = j;
            continue;
        }

        // Parentheses and comma
        if (expr[i] == '(') { tokens.push_back({TokenType::LPAREN,  "("}); i++; continue; }
        if (expr[i] == ')') { tokens.push_back({TokenType::RPAREN,  ")"}); i++; continue; }
        if (expr[i] == ',') { tokens.push_back({TokenType::COMMA,   ","}); i++; continue; }

        // Two-character operators (<>, <=, >=, !=, ||, &&)
        if (i + 1 < n) {
            std::string two = expr.substr(i, 2);
            if (OPERATORS.count(two)) {
                tokens.push_back({TokenType::OPERATOR, two});
                i += 2;
                continue;
            }
        }

        // Single-character operators
        std::string one(1, expr[i]);
        if (OPERATORS.count(one)) {
            tokens.push_back({TokenType::OPERATOR, one});
        } else {
            // Unknown character — skip
        }
        i++;
    }

    return tokens;
}

// ─────────────────────────────────────────────────────────────────────────────
// Shunting-Yard: convert infix token list → postfix (RPN) token list
// ─────────────────────────────────────────────────────────────────────────────
//
// Algorithm (Dijkstra, 1961):
//   Maintain an operator stack and an output queue.
//   For each token:
//     - Number/identifier → push to output
//     - Left paren → push to operator stack
//     - Right paren → pop operators to output until left paren found; discard parens
//     - Operator o1:
//       While the top of the operator stack is an operator o2 with
//         (o2 has higher precedence) OR (same precedence AND o1 is left-associative)
//         AND o2 is not a left paren:
//           pop o2 to output
//       Push o1 to operator stack
//   After all tokens: pop remaining operators to output
//
std::vector<Token> shunting_yard(const std::vector<Token>& tokens) {
    std::vector<Token> output;
    std::stack<Token>  ops;

    for (const auto& tok : tokens) {
        switch (tok.type) {
        case TokenType::NUMBER:
        case TokenType::STRING_LITERAL:
        case TokenType::IDENTIFIER:
            output.push_back(tok);
            break;

        case TokenType::LPAREN:
            ops.push(tok);
            break;

        case TokenType::RPAREN:
            while (!ops.empty() && ops.top().type != TokenType::LPAREN) {
                output.push_back(ops.top()); ops.pop();
            }
            if (ops.empty())
                throw std::runtime_error("Mismatched parentheses: extra ')'");
            ops.pop();  // discard the left paren
            break;

        case TokenType::OPERATOR: {
            const OpInfo& o1 = OPERATORS.at(tok.value);
            while (!ops.empty() && ops.top().type == TokenType::OPERATOR) {
                const OpInfo& o2 = OPERATORS.at(ops.top().value);
                if (o2.precedence > o1.precedence ||
                    (o2.precedence == o1.precedence && !o1.right_assoc)) {
                    output.push_back(ops.top()); ops.pop();
                } else {
                    break;
                }
            }
            ops.push(tok);
            break;
        }

        case TokenType::COMMA:
            break;  // commas are handled at the parser level
        }
    }

    while (!ops.empty()) {
        if (ops.top().type == TokenType::LPAREN)
            throw std::runtime_error("Mismatched parentheses: extra '('");
        output.push_back(ops.top()); ops.pop();
    }

    return output;
}

// ─────────────────────────────────────────────────────────────────────────────
// RPN evaluator: evaluate postfix token list with a variable map
// ─────────────────────────────────────────────────────────────────────────────

using VarMap = std::unordered_map<std::string, double>;

double eval_rpn(const std::vector<Token>& rpn, const VarMap& vars) {
    std::stack<double> stk;

    for (const auto& tok : rpn) {
        if (tok.type == TokenType::OPERATOR) {
            if (stk.size() < 2) throw std::runtime_error("Malformed expression");
            double b = stk.top(); stk.pop();
            double a = stk.top(); stk.pop();

            if      (tok.value == "+"  || tok.value == "OR" )  stk.push(a + b);
            else if (tok.value == "-")   stk.push(a - b);
            else if (tok.value == "*")   stk.push(a * b);
            else if (tok.value == "/")   { if (b == 0) throw std::runtime_error("Division by zero"); stk.push(a / b); }
            else if (tok.value == "%")   stk.push(std::fmod(a, b));
            else if (tok.value == "^")   stk.push(std::pow(a, b));
            else if (tok.value == "<")   stk.push(a < b  ? 1.0 : 0.0);
            else if (tok.value == ">")   stk.push(a > b  ? 1.0 : 0.0);
            else if (tok.value == "<=")  stk.push(a <= b ? 1.0 : 0.0);
            else if (tok.value == ">=")  stk.push(a >= b ? 1.0 : 0.0);
            else if (tok.value == "=" )  stk.push(a == b ? 1.0 : 0.0);
            else if (tok.value == "!=" || tok.value == "<>") stk.push(a != b ? 1.0 : 0.0);
            else if (tok.value == "&&" || tok.value == "AND") stk.push((a != 0 && b != 0) ? 1.0 : 0.0);
            else if (tok.value == "||")  stk.push((a != 0 || b != 0) ? 1.0 : 0.0);
            else throw std::runtime_error("Unknown operator: " + tok.value);
        } else if (tok.type == TokenType::NUMBER) {
            stk.push(std::stod(tok.value));
        } else if (tok.type == TokenType::STRING_LITERAL) {
            // Strings: compare by length for demo purposes (real DB uses string ops)
            stk.push((double)tok.value.size());
        } else {
            // Variable lookup
            auto it = vars.find(tok.value);
            if (it == vars.end())
                throw std::runtime_error("Undefined variable: " + tok.value);
            stk.push(it->second);
        }
    }

    if (stk.empty()) throw std::runtime_error("Empty expression");
    return stk.top();
}

// Convenience: evaluate a raw expression string
double eval_expr(const std::string& expr, const VarMap& vars) {
    auto tokens = tokenize(expr);
    auto rpn    = shunting_yard(tokens);
    return eval_rpn(rpn, vars);
}

// ─────────────────────────────────────────────────────────────────────────────
// Print the RPN tokens for educational display
// ─────────────────────────────────────────────────────────────────────────────

void print_rpn(const std::string& expr) {
    auto tokens = tokenize(expr);
    auto rpn    = shunting_yard(tokens);

    std::cout << "  Expression : " << expr << "\n";
    std::cout << "  Tokens     : ";
    for (auto& t : tokens) std::cout << t.value << " ";
    std::cout << "\n";
    std::cout << "  RPN        : ";
    for (auto& t : rpn) std::cout << t.value << " ";
    std::cout << "\n";
}

// =============================================================================
// PART 2: MINIMAL SQL SELECT PARSER + EXECUTOR
// =============================================================================

// ─────────────────────────────────────────────────────────────────────────────
// Row type: a row is a map from column name → value (double or string)
// ─────────────────────────────────────────────────────────────────────────────

using Value = std::variant<double, std::string>;

struct Row {
    std::unordered_map<std::string, Value> cols;
};

// Extract the double value of a column (string columns → 0.0 for expressions)
double col_as_double(const Row& row, const std::string& col_name) {
    auto it = row.cols.find(col_name);
    if (it == row.cols.end()) return 0.0;
    if (auto* d = std::get_if<double>(&it->second)) return *d;
    if (auto* s = std::get_if<std::string>(&it->second)) {
        try { return std::stod(*s); } catch (...) {}
    }
    return 0.0;
}

std::string col_as_string(const Row& row, const std::string& col_name) {
    auto it = row.cols.find(col_name);
    if (it == row.cols.end()) return "";
    if (auto* s = std::get_if<std::string>(&it->second)) return *s;
    if (auto* d = std::get_if<double>(&it->second)) {
        std::ostringstream oss;
        oss << *d;
        return oss.str();
    }
    return "";
}

// ─────────────────────────────────────────────────────────────────────────────
// Parsed representation of a SELECT query
// ─────────────────────────────────────────────────────────────────────────────

struct SelectQuery {
    std::vector<std::string>  select_cols;  // empty = SELECT *
    std::string               from_table;
    std::string               where_clause; // raw expression string
    std::string               order_by_col;
    bool                      order_asc = true;
    int                       limit_n   = -1; // -1 = no limit
};

// ─────────────────────────────────────────────────────────────────────────────
// SQL Parser: handles SELECT ... FROM ... WHERE ... ORDER BY ... LIMIT ...
// ─────────────────────────────────────────────────────────────────────────────

std::string to_upper(std::string s) {
    for (auto& c : s) c = std::toupper(c);
    return s;
}

SelectQuery parse_select(const std::string& sql) {
    SelectQuery q;
    std::istringstream ss(sql);
    std::string word;

    ss >> word;  // consume SELECT keyword

    // Read column list until FROM
    std::string col;
    while (ss >> col && to_upper(col) != "FROM") {
        // Strip trailing comma
        if (!col.empty() && col.back() == ',') col.pop_back();
        if (col == "*") q.select_cols.clear();
        else            q.select_cols.push_back(col);
    }

    ss >> q.from_table;  // table name

    // Parse optional clauses (WHERE, ORDER BY, LIMIT)
    while (ss >> word) {
        std::string kw = to_upper(word);

        if (kw == "WHERE") {
            // Consume everything until ORDER, LIMIT, or end-of-string
            std::string clause;
            std::streampos mark;
            while (ss >> word) {
                if (to_upper(word) == "ORDER" || to_upper(word) == "LIMIT") {
                    kw = to_upper(word);
                    goto after_where;
                }
                if (!clause.empty()) clause += " ";
                clause += word;
            }
            q.where_clause = clause;
            break;
        after_where:
            q.where_clause = clause;
            // fall through to handle ORDER/LIMIT
        }

        if (kw == "ORDER") {
            ss >> word;  // BY
            ss >> q.order_by_col;
            std::string dir;
            if (ss >> dir) {
                if (to_upper(dir) == "DESC") q.order_asc = false;
                // else ASC (default)
            }
            // Check for LIMIT after ORDER BY
            if (ss >> word && to_upper(word) == "LIMIT")
                ss >> q.limit_n;
        }

        if (kw == "LIMIT") {
            ss >> q.limit_n;
        }
    }

    return q;
}

// ─────────────────────────────────────────────────────────────────────────────
// Executor: run the parsed query against in-memory data
// ─────────────────────────────────────────────────────────────────────────────

std::vector<Row> execute(const SelectQuery& q, const std::vector<Row>& data) {
    // Pre-compile the WHERE clause into RPN (do it once, not per row)
    std::vector<Token> where_rpn;
    if (!q.where_clause.empty()) {
        try {
            where_rpn = shunting_yard(tokenize(q.where_clause));
        } catch (const std::exception& e) {
            std::cerr << "[PARSE ERROR in WHERE] " << e.what() << "\n";
        }
    }

    std::vector<Row> result;

    for (const auto& row : data) {
        // ── WHERE filter ──────────────────────────────────────────────────────
        if (!where_rpn.empty()) {
            // Build variable map: column_name → numeric value
            VarMap vars;
            for (const auto& [col, val] : row.cols)
                vars[col] = col_as_double(row, col);

            try {
                double passed = eval_rpn(where_rpn, vars);
                if (passed == 0.0) continue;  // row rejected
            } catch (...) {
                continue;  // on error, reject the row
            }
        }

        // ── SELECT projection ─────────────────────────────────────────────────
        if (q.select_cols.empty()) {
            result.push_back(row);  // SELECT *
        } else {
            Row projected;
            for (const auto& col : q.select_cols) {
                auto it = row.cols.find(col);
                if (it != row.cols.end())
                    projected.cols[col] = it->second;
            }
            result.push_back(projected);
        }
    }

    // ── ORDER BY sort ─────────────────────────────────────────────────────────
    if (!q.order_by_col.empty()) {
        std::stable_sort(result.begin(), result.end(),
            [&](const Row& a, const Row& b) {
                // Try numeric sort first
                double va = col_as_double(a, q.order_by_col);
                double vb = col_as_double(b, q.order_by_col);
                if (va != vb)
                    return q.order_asc ? va < vb : va > vb;
                // Fall back to string sort for tied numeric values
                std::string sa = col_as_string(a, q.order_by_col);
                std::string sb = col_as_string(b, q.order_by_col);
                return q.order_asc ? sa < sb : sa > sb;
            });
    }

    // ── LIMIT truncation ──────────────────────────────────────────────────────
    if (q.limit_n >= 0 && (int)result.size() > q.limit_n)
        result.resize(q.limit_n);

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Print utilities
// ─────────────────────────────────────────────────────────────────────────────

void print_row(const Row& row, const std::vector<std::string>& col_order) {
    for (const auto& col : col_order) {
        auto it = row.cols.find(col);
        if (it == row.cols.end()) { std::cout << std::setw(12) << "NULL"; continue; }
        if (auto* d = std::get_if<double>(&it->second)) {
            if (*d == (int)*d) std::cout << std::setw(12) << (int)*d;
            else               std::cout << std::setw(12) << std::fixed << std::setprecision(2) << *d;
        } else if (auto* s = std::get_if<std::string>(&it->second)) {
            std::cout << std::setw(14) << *s;
        }
    }
    std::cout << "\n";
}

void print_results(const std::vector<Row>& rows,
                   const std::vector<std::string>& col_order,
                   const std::string& sql) {
    std::cout << "\nSQL: " << sql << "\n";
    std::cout << std::string(70, '-') << "\n";
    for (const auto& col : col_order)
        std::cout << std::setw(col.size() < 12 ? 12 : 14) << col;
    std::cout << "\n" << std::string(70, '-') << "\n";
    for (const auto& row : rows) print_row(row, col_order);
    std::cout << "(" << rows.size() << " rows)\n";
}

// =============================================================================
// MAIN: Demonstrations
// =============================================================================

int main() {
    std::cout << "=== Lab 5 — Shunting-Yard + SQL Parser ===\n"
              << "    Indrajeet Yadav | 23BCS10199\n\n";

    // ─────────────────────────────────────────────────────────────────────────
    // Part 1: Shunting-Yard Algorithm Demos
    // ─────────────────────────────────────────────────────────────────────────
    std::cout << "╔══════════════════════════════════════════════════════╗\n"
              << "  PART 1: Shunting-Yard — Infix to RPN\n"
              << "╚══════════════════════════════════════════════════════╝\n\n";

    struct ExprTest { std::string expr; VarMap vars; };

    std::vector<ExprTest> expr_tests = {
        {"3 + 4 * 2",              {}},
        {"(3 + 4) * 2",            {}},
        {"2 ^ 3 ^ 2",              {}},    // right-assoc: 2^(3^2) = 512
        {"age * 2 > 50 AND gpa >= 3.5",  {{"age", 30.0}, {"gpa", 3.8}}},
        {"salary / 1000 + bonus * 0.1 > 100", {{"salary", 80000.0}, {"bonus", 5000.0}}},
        {"(a + b) * (c - d) / 2",  {{"a", 10.0}, {"b", 5.0}, {"c", 20.0}, {"d", 8.0}}},
    };

    for (auto& [expr, vars] : expr_tests) {
        print_rpn(expr);
        if (!vars.empty()) {
            std::cout << "  Variables  : ";
            for (auto& [k, v] : vars) std::cout << k << "=" << v << " ";
            std::cout << "\n";
            double result = eval_expr(expr, vars);
            std::cout << "  Result     : " << result
                      << (result != 0 ? "  (true)" : "  (false)") << "\n";
        } else {
            VarMap empty;
            double result = eval_expr(expr, empty);
            std::cout << "  Result     : " << result << "\n";
        }
        std::cout << "\n";
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Part 2: SQL Parser + Executor Demos
    // ─────────────────────────────────────────────────────────────────────────
    std::cout << "╔══════════════════════════════════════════════════════╗\n"
              << "  PART 2: SQL SELECT Parser + In-Memory Executor\n"
              << "╚══════════════════════════════════════════════════════╝\n";

    // Sample dataset: student records
    std::vector<Row> students = {
        {{{ "id", 1.0 }, { "name", std::string("Indrajeet")  }, { "age", 21.0 }, { "gpa", 9.1 }, { "country", std::string("India")  }, { "credits", 120.0 }}},
        {{{ "id", 2.0 }, { "name", std::string("Alice")      }, { "age", 22.0 }, { "gpa", 8.7 }, { "country", std::string("India")  }, { "credits", 110.0 }}},
        {{{ "id", 3.0 }, { "name", std::string("Bob")        }, { "age", 25.0 }, { "gpa", 7.5 }, { "country", std::string("USA")    }, { "credits", 90.0  }}},
        {{{ "id", 4.0 }, { "name", std::string("Carol")      }, { "age", 21.0 }, { "gpa", 9.4 }, { "country", std::string("India")  }, { "credits", 125.0 }}},
        {{{ "id", 5.0 }, { "name", std::string("Dave")       }, { "age", 30.0 }, { "gpa", 6.8 }, { "country", std::string("USA")    }, { "credits", 80.0  }}},
        {{{ "id", 6.0 }, { "name", std::string("Eve")        }, { "age", 23.0 }, { "gpa", 8.2 }, { "country", std::string("UK")     }, { "credits", 100.0 }}},
        {{{ "id", 7.0 }, { "name", std::string("Frank")      }, { "age", 24.0 }, { "gpa", 7.9 }, { "country", std::string("UK")     }, { "credits", 95.0  }}},
        {{{ "id", 8.0 }, { "name", std::string("Grace")      }, { "age", 20.0 }, { "gpa", 9.8 }, { "country", std::string("India")  }, { "credits", 130.0 }}},
    };

    // Query 1: All students with gpa > 8.0, sorted by gpa descending
    {
        std::string sql = "SELECT id, name, gpa FROM students WHERE gpa > 8.0 ORDER BY gpa DESC";
        auto q   = parse_select(sql);
        auto res = execute(q, students);
        print_results(res, {"id", "name", "gpa"}, sql);
    }

    // Query 2: Students aged between 21 and 24 (inclusive), ordered by age
    {
        std::string sql = "SELECT id, name, age, country FROM students WHERE age >= 21 AND age <= 24 ORDER BY age ASC";
        auto q   = parse_select(sql);
        auto res = execute(q, students);
        print_results(res, {"id", "name", "age", "country"}, sql);
    }

    // Query 3: Students with gpa > 8.5 AND credits >= 110, limit 3
    {
        std::string sql = "SELECT id, name, gpa, credits FROM students WHERE gpa > 8.5 AND credits >= 110 ORDER BY credits DESC LIMIT 3";
        auto q   = parse_select(sql);
        auto res = execute(q, students);
        print_results(res, {"id", "name", "gpa", "credits"}, sql);
    }

    // Query 4: SELECT * (all columns) — no WHERE
    {
        std::string sql = "SELECT * FROM students ORDER BY id ASC";
        auto q   = parse_select(sql);
        auto res = execute(q, students);
        // Print a few key columns for readability
        print_results(res, {"id", "name", "age", "gpa", "credits"}, sql);
    }

    // Query 5: Compound arithmetic in WHERE
    {
        std::string sql = "SELECT id, name, gpa, age FROM students WHERE gpa * age > 190 ORDER BY gpa DESC LIMIT 4";
        auto q   = parse_select(sql);
        auto res = execute(q, students);
        print_results(res, {"id", "name", "gpa", "age"}, sql);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Architecture summary
    // ─────────────────────────────────────────────────────────────────────────
    std::cout << "\n╔══════════════════════════════════════════════════════╗\n"
              << "  HOW THIS MAPS TO A REAL DATABASE\n"
              << "╚══════════════════════════════════════════════════════╝\n\n"
              << "  SQL String\n"
              << "      │\n"
              << "  Lexer/Tokenizer  ←  tokenize()         [this lab]\n"
              << "      │\n"
              << "  Parser           ←  parse_select()     [this lab]\n"
              << "      │  Produces a SelectQuery AST\n"
              << "      │\n"
              << "  Planner          ←  (not implemented)\n"
              << "      │  Would decide: index scan vs full scan,\n"
              << "      │  join order, parallel vs serial, etc.\n"
              << "      │\n"
              << "  Executor         ←  execute()          [this lab]\n"
              << "      │  WHERE: eval_rpn() per row (Shunting-Yard output)\n"
              << "      │  SELECT: column projection\n"
              << "      │  ORDER BY: std::stable_sort on result set\n"
              << "      │  LIMIT: vector::resize()\n"
              << "      │\n"
              << "  Storage Layer    ←  vector<Row>        [simulated]\n"
              << "      │  In a real DB: heap pages from the buffer pool\n"
              << "      │  (as in Lab 2 SQLite internals, Lab 3 ClockSweep)\n"
              << "      │\n"
              << "  Result Set       ←  vector<Row>\n\n"
              << "  Key insight: Shunting-Yard converts the WHERE clause\n"
              << "  to RPN ONCE at parse time. Evaluation is a simple stack\n"
              << "  machine pass — O(n) per row, no recursion needed.\n"
              << "  Real databases compile the WHERE expression into a tree\n"
              << "  of expression nodes (ExprContext in PostgreSQL), but the\n"
              << "  fundamental mechanism is the same.\n\n";

    return 0;
}
