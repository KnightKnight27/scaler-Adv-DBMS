#include <iostream>
#include <sstream>
#include <stack>
#include <vector>
#include <string>
#include <unordered_map>
#include <variant>
#include <algorithm>
#include <functional>
#include <stdexcept>
#include <cctype>
#include <cmath>

/**
 * Lab 5: Shunting-Yard Algorithm + SQL SELECT Parser
 * 
 * Part 1: Dijkstra's Shunting-Yard Algorithm
 * - Converts infix expressions to postfix (RPN)
 * - Evaluates expressions with operator precedence
 * 
 * Part 2: Minimal SQL Parser
 * - Parses SELECT queries
 * - Executes over vector<Row> in memory
 * - Supports WHERE, ORDER BY, LIMIT
 */

// ============================================================================
// Part 1: Shunting-Yard Algorithm (Expression Evaluator)
// ============================================================================

/**
 * OpInfo: Operator metadata
 * - precedence: Higher number = higher precedence
 * - right_assoc: True for right-associative operators (e.g., ^)
 */
struct OpInfo {
    int  precedence;
    bool right_assoc;
};

/**
 * Operator precedence table (C-style)
 */
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

/**
 * tokenize: Split expression into tokens
 * Handles: numbers, identifiers, operators, parentheses
 */
std::vector<std::string> tokenize(const std::string& expr) {
    std::vector<std::string> tokens;
    int i = 0, n = expr.size();
    
    while (i < n) {
        // Skip whitespace
        if (std::isspace(expr[i])) {
            i++;
            continue;
        }
        
        // Numbers (including decimals)
        if (std::isdigit(expr[i]) || (expr[i] == '.' && i+1 < n && std::isdigit(expr[i+1]))) {
            int j = i;
            while (j < n && (std::isdigit(expr[j]) || expr[j] == '.'))
                j++;
            tokens.push_back(expr.substr(i, j - i));
            i = j;
        }
        // Identifiers (variables, column names)
        else if (std::isalpha(expr[i]) || expr[i] == '_') {
            int j = i;
            while (j < n && (std::isalnum(expr[j]) || expr[j] == '_'))
                j++;
            tokens.push_back(expr.substr(i, j - i));
            i = j;
        }
        // Parentheses
        else if (expr[i] == '(' || expr[i] == ')') {
            tokens.push_back(std::string(1, expr[i++]));
        }
        // Operators (check two-char operators first)
        else {
            if (i+1 < n) {
                std::string two = expr.substr(i, 2);
                if (OPS.count(two)) {
                    tokens.push_back(two);
                    i += 2;
                    continue;
                }
            }
            tokens.push_back(std::string(1, expr[i++]));
        }
    }
    
    return tokens;
}

/**
 * to_rpn: Convert infix tokens to postfix (Reverse Polish Notation)
 * Uses Dijkstra's Shunting-Yard algorithm
 */
std::vector<std::string> to_rpn(const std::vector<std::string>& tokens) {
    std::vector<std::string> output;
    std::stack<std::string>  ops;
    
    for (const auto& tok : tokens) {
        if (tok == "(") {
            ops.push(tok);
        }
        else if (tok == ")") {
            while (!ops.empty() && ops.top() != "(") {
                output.push_back(ops.top());
                ops.pop();
            }
            if (ops.empty())
                throw std::runtime_error("Mismatched parentheses");
            ops.pop(); // discard '('
        }
        else if (OPS.count(tok)) {
            const auto& o1 = OPS.at(tok);
            while (!ops.empty() && OPS.count(ops.top())) {
                const auto& o2 = OPS.at(ops.top());
                if (o2.precedence > o1.precedence ||
                   (o2.precedence == o1.precedence && !o1.right_assoc)) {
                    output.push_back(ops.top());
                    ops.pop();
                } else {
                    break;
                }
            }
            ops.push(tok);
        }
        else {
            // Number or identifier (operand)
            output.push_back(tok);
        }
    }
    
    while (!ops.empty()) {
        if (ops.top() == "(")
            throw std::runtime_error("Mismatched parentheses");
        output.push_back(ops.top());
        ops.pop();
    }
    
    return output;
}

/**
 * eval_rpn: Evaluate RPN expression with variable map
 * All values treated as doubles for simplicity
 */
double eval_rpn(const std::vector<std::string>& rpn,
                const std::unordered_map<std::string, double>& vars) {
    std::stack<double> stk;
    
    for (const auto& tok : rpn) {
        if (OPS.count(tok)) {
            if (stk.size() < 2)
                throw std::runtime_error("Invalid expression");
            
            double b = stk.top(); stk.pop();
            double a = stk.top(); stk.pop();
            
            if      (tok == "+")  stk.push(a + b);
            else if (tok == "-")  stk.push(a - b);
            else if (tok == "*")  stk.push(a * b);
            else if (tok == "/")  stk.push(b != 0 ? a / b : 0);
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
            // Try to parse as number
            try {
                stk.push(std::stod(tok));
            } catch (...) {
                // Must be a variable
                auto it = vars.find(tok);
                if (it == vars.end())
                    throw std::runtime_error("Unknown variable: " + tok);
                stk.push(it->second);
            }
        }
    }
    
    if (stk.size() != 1)
        throw std::runtime_error("Invalid expression");
    
    return stk.top();
}


// ============================================================================
// Part 2: SQL Parser and Executor
// ============================================================================

/**
 * Value: Can hold either a number or a string
 */
using Value = std::variant<double, std::string>;

/**
 * Row: A single database row (column_name -> value)
 */
struct Row {
    std::unordered_map<std::string, Value> cols;
};

/**
 * SelectQuery: Parsed representation of SELECT statement
 */
struct SelectQuery {
    std::vector<std::string>  columns;     // empty = SELECT *
    std::string               from;        // table name
    std::string               where_raw;   // raw WHERE clause
    std::string               order_by;    // column to order by
    bool                      order_asc = true;
    int                       limit = -1;
};

/**
 * to_upper: Convert string to uppercase
 */
std::string to_upper(std::string s) {
    for (auto& c : s)
        c = std::toupper(c);
    return s;
}

/**
 * row_val: Extract numeric value from row column
 * Used for expression evaluation
 */
double row_val(const Row& row, const std::string& col) {
    auto it = row.cols.find(col);
    if (it == row.cols.end())
        return 0.0;
    
    if (auto* d = std::get_if<double>(&it->second))
        return *d;
    
    if (auto* s = std::get_if<std::string>(&it->second)) {
        try { return std::stod(*s); }
        catch (...) { return 0.0; }
    }
    
    return 0.0;
}

/**
 * parse_select: Parse SQL SELECT statement
 * 
 * Supported syntax:
 * SELECT col1, col2, ... | * FROM table
 * [WHERE condition]
 * [ORDER BY column [ASC|DESC]]
 * [LIMIT n]
 */
SelectQuery parse_select(const std::string& sql) {
    SelectQuery q;
    std::istringstream ss(sql);
    std::string word;
    
    ss >> word; // SELECT
    if (to_upper(word) != "SELECT")
        throw std::runtime_error("Query must start with SELECT");
    
    // Read column list until FROM
    std::string col_buf;
    while (ss >> word && to_upper(word) != "FROM") {
        // Strip trailing comma
        if (!word.empty() && word.back() == ',')
            word.pop_back();
        
        if (word == "*")
            q.columns.clear();
        else if (!word.empty())
            q.columns.push_back(word);
    }
    
    ss >> q.from; // table name
    
    // Read optional clauses
    while (ss >> word) {
        std::string kw = to_upper(word);
        
        if (kw == "WHERE") {
            // Consume until ORDER/LIMIT/end
            std::string clause, w2;
            while (ss >> w2) {
                std::string next_kw = to_upper(w2);
                if (next_kw == "ORDER" || next_kw == "LIMIT") {
                    word = w2;
                    goto next_clause;
                }
                clause += (clause.empty() ? "" : " ") + w2;
            }
            q.where_raw = clause;
            break;
            
        next_clause:
            q.where_raw = clause;
            kw = to_upper(word);
        }
        
        if (kw == "ORDER") {
            ss >> word; // BY
            ss >> q.order_by;
            std::string dir;
            if (ss >> dir && to_upper(dir) == "DESC")
                q.order_asc = false;
        }
        
        if (kw == "LIMIT") {
            ss >> q.limit;
        }
    }
    
    return q;
}

/**
 * execute: Execute parsed SELECT query against in-memory data
 * 
 * Query execution pipeline:
 * 1. Filter (WHERE clause)
 * 2. Project (SELECT columns)
 * 3. Sort (ORDER BY)
 * 4. Limit (LIMIT n)
 */
std::vector<Row> execute(const SelectQuery& q, const std::vector<Row>& data) {
    // Compile WHERE clause to RPN once
    std::vector<std::string> rpn;
    if (!q.where_raw.empty())
        rpn = to_rpn(tokenize(q.where_raw));
    
    std::vector<Row> result;
    
    // Step 1: Filter + Project
    for (const auto& row : data) {
        // Evaluate WHERE clause
        if (!rpn.empty()) {
            // Build variable map from row
            std::unordered_map<std::string, double> vars;
            for (const auto& [k, v] : row.cols)
                vars[k] = row_val(row, k);
            
            double filter_result = eval_rpn(rpn, vars);
            if (filter_result == 0.0)  // false
                continue;
        }
        
        // Project columns
        if (q.columns.empty()) {
            // SELECT * - return all columns
            result.push_back(row);
        } else {
            // SELECT col1, col2, ... - return only specified columns
            Row projected;
            for (const auto& col : q.columns) {
                if (row.cols.count(col))
                    projected.cols[col] = row.cols.at(col);
            }
            result.push_back(projected);
        }
    }
    
    // Step 2: Sort (ORDER BY)
    if (!q.order_by.empty()) {
        std::sort(result.begin(), result.end(), [&](const Row& a, const Row& b) {
            double va = row_val(a, q.order_by);
            double vb = row_val(b, q.order_by);
            return q.order_asc ? va < vb : va > vb;
        });
    }
    
    // Step 3: Limit
    if (q.limit >= 0 && (int)result.size() > q.limit)
        result.resize(q.limit);
    
    return result;
}

/**
 * print_rows: Display query results
 */
void print_rows(const std::vector<Row>& rows) {
    if (rows.empty()) {
        std::cout << "(0 rows)" << std::endl;
        return;
    }
    
    for (const auto& row : rows) {
        for (const auto& [k, v] : row.cols) {
            std::cout << k << "=";
            if (auto* d = std::get_if<double>(&v))
                std::cout << *d;
            if (auto* s = std::get_if<std::string>(&v))
                std::cout << *s;
            std::cout << "  ";
        }
        std::cout << std::endl;
    }
    std::cout << "(" << rows.size() << " rows)" << std::endl;
}


// ============================================================================
// Test Suite
// ============================================================================

void shunting_demo() {
    std::cout << "=== Part 1: Shunting-Yard Algorithm ===" << std::endl;
    std::cout << "Converting infix → postfix → evaluation\n" << std::endl;
    
    std::vector<std::string> test_exprs = {
        "age * 2 + salary / 1000 > 100",
        "3 + 4 * 2",
        "(3 + 4) * 2",
        "2 ^ 3 ^ 2",  // Right-associative
        "gpa >= 3.5 && age < 25"
    };
    
    for (const auto& expr : test_exprs) {
        std::cout << "Expression: " << expr << std::endl;
        
        auto tokens = tokenize(expr);
        std::cout << "Tokens: ";
        for (const auto& t : tokens)
            std::cout << t << " ";
        std::cout << std::endl;
        
        auto rpn = to_rpn(tokens);
        std::cout << "RPN: ";
        for (const auto& t : rpn)
            std::cout << t << " ";
        std::cout << std::endl;
        
        // Evaluate with sample variables
        std::unordered_map<std::string, double> vars = {
            {"age", 30}, {"salary", 50000}, {"gpa", 3.8}
        };
        
        try {
            double result = eval_rpn(rpn, vars);
            std::cout << "Result: " << result;
            if (expr.find('>') != std::string::npos || 
                expr.find('<') != std::string::npos ||
                expr.find("&&") != std::string::npos)
                std::cout << " (" << (result ? "true" : "false") << ")";
            std::cout << std::endl;
        } catch (const std::exception& e) {
            std::cout << "Evaluation: " << e.what() << std::endl;
        }
        
        std::cout << std::endl;
    }
}

int main() {
    std::cout << "=== Lab 5: Shunting-Yard + SQL Parser ===" << std::endl;
    std::cout << "Expression evaluation and query processing\n" << std::endl;
    
    // Demo Shunting-Yard algorithm
    shunting_demo();
    
    // Demo SQL Parser
    std::cout << "\n=== Part 2: SQL SELECT Parser & Executor ===" << std::endl;
    std::cout << "Query execution over vector<Row>\n" << std::endl;
    
    // Create sample data
    std::vector<Row> students = {
        {{{ "id", 1.0 }, { "name", std::string("Alice") }, { "age", 22.0 }, { "gpa", 3.8 }}},
        {{{ "id", 2.0 }, { "name", std::string("Bob")   }, { "age", 25.0 }, { "gpa", 2.9 }}},
        {{{ "id", 3.0 }, { "name", std::string("Carol") }, { "age", 21.0 }, { "gpa", 3.5 }}},
        {{{ "id", 4.0 }, { "name", std::string("Dave")  }, { "age", 30.0 }, { "gpa", 3.1 }}},
        {{{ "id", 5.0 }, { "name", std::string("Eve")   }, { "age", 23.0 }, { "gpa", 3.9 }}},
    };
    
    std::cout << "Sample data: 5 students" << std::endl << std::endl;
    
    // Test queries
    std::vector<std::string> queries = {
        "SELECT * FROM students",
        "SELECT name, gpa FROM students WHERE gpa > 3.0",
        "SELECT name, age, gpa FROM students WHERE gpa > 3.5 ORDER BY gpa DESC",
        "SELECT name, gpa FROM students WHERE age >= 22 && age <= 26 ORDER BY gpa DESC LIMIT 2",
        "SELECT * FROM students WHERE gpa >= 3.5 && age < 25"
    };
    
    for (const auto& sql : queries) {
        std::cout << "┌────────────────────────────────────────────────────┐" << std::endl;
        std::cout << "│ SQL: " << sql << std::endl;
        std::cout << "└────────────────────────────────────────────────────┘" << std::endl;
        
        try {
            auto query = parse_select(sql);
            auto result = execute(query, students);
            print_rows(result);
        } catch (const std::exception& e) {
            std::cout << "Error: " << e.what() << std::endl;
        }
        
        std::cout << std::endl;
    }
    
    std::cout << "✓ All tests complete!" << std::endl;
    
    return 0;
}
