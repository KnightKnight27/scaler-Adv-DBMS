#include <iostream>
#include <sstream>
#include <stack>
#include <vector>
#include <string>
#include <unordered_map>
#include <stdexcept>
#include <cctype>
#include <cmath>

// ─────────────────────────────────────────────
// 1. Shunting-Yard & Expression Evaluator
// ─────────────────────────────────────────────

struct OpInfo {
    int precedence;
    bool right_assoc;
};

// Define operator precedences and associativity
const std::unordered_map<std::string, OpInfo> OPS = {
    {"||", {1, false}},   // Logical OR
    {"&&", {2, false}},   // Logical AND
    {"=",  {3, false}},   // Equality
    {"!=", {3, false}},   // Inequality
    {"<",  {4, false}},
    {">",  {4, false}},
    {"<=", {4, false}},
    {">=", {4, false}},
    {"+",  {5, false}},
    {"-",  {5, false}},
    {"*",  {6, false}},
    {"/",  {6, false}},
    {"^",  {7, true }}    // Exponentiation (right-associative)
};

// Tokenizer
std::vector<std::string> tokenize(const std::string& expr) {
    std::vector<std::string> tokens;
    int i = 0, n = expr.size();
    while (i < n) {
        if (std::isspace(expr[i])) { 
            i++; 
            continue; 
        }
        
        // Parse numbers (including decimals)
        if (std::isdigit(expr[i]) || (expr[i] == '.' && i + 1 < n && std::isdigit(expr[i + 1]))) {
            int j = i;
            while (j < n && (std::isdigit(expr[j]) || expr[j] == '.')) j++;
            tokens.push_back(expr.substr(i, j - i));
            i = j;
        } 
        // Parse identifiers (variables/column names)
        else if (std::isalpha(expr[i]) || expr[i] == '_') {
            int j = i;
            while (j < n && (std::isalnum(expr[j]) || expr[j] == '_')) j++;
            tokens.push_back(expr.substr(i, j - i));
            i = j;
        } 
        // Parse parenthesis
        else if (expr[i] == '(' || expr[i] == ')') {
            tokens.push_back(std::string(1, expr[i++]));
        } 
        // Parse operators
        else {
            // Check for two-character operators
            if (i + 1 < n) {
                std::string two = expr.substr(i, 2);
                if (OPS.count(two)) { 
                    tokens.push_back(two); 
                    i += 2; 
                    continue; 
                }
            }
            // Single character operator
            tokens.push_back(std::string(1, expr[i++]));
        }
    }
    return tokens;
}

// Convert Infix to Postfix (Reverse Polish Notation) using Shunting-Yard
std::vector<std::string> to_rpn(const std::vector<std::string>& tokens) {
    std::vector<std::string> output;
    std::stack<std::string> ops;

    for (const auto& tok : tokens) {
        if (tok == "(") {
            ops.push(tok);
        } else if (tok == ")") {
            while (!ops.empty() && ops.top() != "(") {
                output.push_back(ops.top());
                ops.pop();
            }
            if (ops.empty()) throw std::runtime_error("Mismatched parentheses");
            ops.pop(); // discard '('
        } else if (OPS.count(tok)) {
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
        } else {
            // Numbers and variables directly go to output
            output.push_back(tok); 
        }
    }
    
    while (!ops.empty()) {
        if (ops.top() == "(") throw std::runtime_error("Mismatched parentheses");
        output.push_back(ops.top());
        ops.pop();
    }
    return output;
}

// Evaluate RPN expression given a context map of variables
double eval_rpn(const std::vector<std::string>& rpn, const std::unordered_map<std::string, double>& env) {
    std::stack<double> stack;

    for (const auto& tok : rpn) {
        if (OPS.count(tok)) {
            if (stack.size() < 2) throw std::runtime_error("Invalid RPN expression (too few arguments)");
            double b = stack.top(); stack.pop();
            double a = stack.top(); stack.pop();
            
            if (tok == "+") stack.push(a + b);
            else if (tok == "-") stack.push(a - b);
            else if (tok == "*") stack.push(a * b);
            else if (tok == "/") {
                if (b == 0) throw std::runtime_error("Division by zero");
                stack.push(a / b);
            }
            else if (tok == "^")  stack.push(std::pow(a, b));
            else if (tok == "=")  stack.push(a == b);
            else if (tok == "!=") stack.push(a != b);
            else if (tok == "<")  stack.push(a < b);
            else if (tok == ">")  stack.push(a > b);
            else if (tok == "<=") stack.push(a <= b);
            else if (tok == ">=") stack.push(a >= b);
            else if (tok == "&&") stack.push((a != 0.0) && (b != 0.0));
            else if (tok == "||") stack.push((a != 0.0) || (b != 0.0));
        } else {
            // If token is a variable, fetch its value, otherwise parse it as a number
            if (env.count(tok)) {
                stack.push(env.at(tok));
            } else {
                stack.push(std::stod(tok));
            }
        }
    }

    if (stack.size() != 1) throw std::runtime_error("Invalid RPN expression (too many values left on stack)");
    return stack.top();
}


// ─────────────────────────────────────────────
// 2. Minimal SQL SELECT Parser
// ─────────────────────────────────────────────

using Row = std::unordered_map<std::string, double>;

std::vector<Row> execute_select(const std::vector<Row>& table, const std::string& where_clause) {
    auto tokens = tokenize(where_clause);
    auto rpn = to_rpn(tokens);
    
    std::vector<Row> result;
    for (const auto& row : table) {
        // If the WHERE condition evaluates to non-zero (true), keep the row
        if (eval_rpn(rpn, row) != 0.0) {
            result.push_back(row);
        }
    }
    return result;
}

// ─────────────────────────────────────────────
// 3. Testing
// ─────────────────────────────────────────────

int main() {
    std::vector<Row> users = {
        {{"id", 1}, {"age", 20}, {"salary", 50000}},
        {{"id", 2}, {"age", 26}, {"salary", 95000}},
        {{"id", 3}, {"age", 30}, {"salary", 110000}},
        {{"id", 4}, {"age", 24}, {"salary", 80000}}
    };

    std::string condition = "age > 25 && salary * 1.1 >= 100000";
    std::cout << "Executing SQL WHERE: " << condition << "\n";
    
    try {
        auto result = execute_select(users, condition);
        std::cout << "Rows matched:\n";
        for (const auto& row : result) {
            std::cout << "  id=" << row.at("id") << "  age=" << row.at("age") 
                      << "  salary=" << row.at("salary") << "\n";
        }
    } catch(const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
    }

    return 0;
}
