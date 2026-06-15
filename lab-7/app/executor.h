#pragma once

#include "types.h"
#include "parser.h"
#include <vector>
#include <algorithm>
#include <iostream>
#include <stack>
#include <unordered_map>
#include <cctype>
#include <cmath>

// Helper: get numeric value from Row
inline double row_val(const Row& row, const std::string& col) {
    auto it = row.cols.find(col);
    if (it == row.cols.end()) return 0.0;
    
    if (auto* d = std::get_if<double>(&it->second)) {
        return *d;
    }
    if (auto* s = std::get_if<std::string>(&it->second)) {
        try { return std::stod(*s); } catch (...) { }
    }
    return 0.0;
}

// Shunting-Yard Expression Evaluator
class ExpressionEvaluator {
public:
    ExpressionEvaluator() {
        operators["||"] = {1, false};
        operators["&&"] = {2, false};
        operators["="]  = {3, false};
        operators["!="] = {3, false};
        operators["<"]  = {4, false};
        operators[">"]  = {4, false};
        operators["<="] = {4, false};
        operators[">="] = {4, false};
        operators["+"]  = {5, false};
        operators["-"]  = {5, false};
        operators["*"]  = {6, false};
        operators["/"]  = {6, false};
        operators["^"]  = {7, true };
    }
    
    std::vector<std::string> tokenize(const std::string& expr);
    std::vector<std::string> to_rpn(const std::vector<std::string>& tokens);
    double eval_rpn(const std::vector<std::string>& rpn,
                   const std::unordered_map<std::string, double>& vars);
    double evaluate(const std::string& expr,
                   const std::unordered_map<std::string, double>& vars);
    
private:
    struct OpInfo { int precedence; bool right_assoc; };
    std::unordered_map<std::string, OpInfo> operators;
    
    bool isOperator(const std::string& token) const {
        return operators.count(token) > 0;
    }
};

// ============================================================================
// INLINE IMPLEMENTATIONS
// ============================================================================

inline std::vector<std::string> ExpressionEvaluator::tokenize(const std::string& expr) {
    std::vector<std::string> tokens;
    int i = 0, n = expr.size();
    
    while (i < n) {
        if (std::isspace(expr[i])) { i++; continue; }
        
        if (std::isdigit(expr[i]) || (expr[i] == '.' && i+1 < n && std::isdigit(expr[i+1]))) {
            int j = i;
            while (j < n && (std::isdigit(expr[j]) || expr[j] == '.')) j++;
            tokens.push_back(expr.substr(i, j - i));
            i = j;
        }
        else if (std::isalpha(expr[i]) || expr[i] == '_') {
            int j = i;
            while (j < n && (std::isalnum(expr[j]) || expr[j] == '_')) j++;
            tokens.push_back(expr.substr(i, j - i));
            i = j;
        }
        else if (expr[i] == '(' || expr[i] == ')') {
            tokens.push_back(std::string(1, expr[i++]));
        }
        else if (i+1 < n) {
            std::string two = expr.substr(i, 2);
            if (operators.count(two)) { 
                tokens.push_back(two); 
                i += 2; 
                continue; 
            }
            tokens.push_back(std::string(1, expr[i++]));
        }
        else {
            tokens.push_back(std::string(1, expr[i++]));
        }
    }
    
    return tokens;
}

inline std::vector<std::string> ExpressionEvaluator::to_rpn(const std::vector<std::string>& tokens) {
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
            if (ops.empty()) throw std::runtime_error("Mismatched parentheses");
            ops.pop();
        } 
        else if (isOperator(tok)) {
            const auto& o1 = operators.at(tok);
            while (!ops.empty() && isOperator(ops.top())) {
                const auto& o2 = operators.at(ops.top());
                if (o2.precedence > o1.precedence ||
                   (o2.precedence == o1.precedence && !o1.right_assoc)) {
                    output.push_back(ops.top()); 
                    ops.pop();
                } else break;
            }
            ops.push(tok);
        } 
        else {
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

inline double ExpressionEvaluator::eval_rpn(const std::vector<std::string>& rpn,
                                           const std::unordered_map<std::string, double>& vars) {
    std::stack<double> stk;
    
    for (const auto& tok : rpn) {
        if (isOperator(tok)) {
            double b = stk.top(); stk.pop();
            double a = stk.top(); stk.pop();
            
            if      (tok == "+")  stk.push(a + b);
            else if (tok == "-")  stk.push(a - b);
            else if (tok == "*")  stk.push(a * b);
            else if (tok == "/")  stk.push(a / b);
            else if (tok == "^")  stk.push(std::pow(a, b));
            else if (tok == "<")  stk.push(a < b  ? 1.0 : 0.0);
            else if (tok == ">")  stk.push(a > b  ? 1.0 : 0.0);
            else if (tok == "<=") stk.push(a <= b ? 1.0 : 0.0);
            else if (tok == ">=") stk.push(a >= b ? 1.0 : 0.0);
            else if (tok == "=")  stk.push(a == b ? 1.0 : 0.0);
            else if (tok == "!=") stk.push(a != b ? 1.0 : 0.0);
            else if (tok == "&&") stk.push((a != 0.0 && b != 0.0) ? 1.0 : 0.0);
            else if (tok == "||") stk.push((a != 0.0 || b != 0.0) ? 1.0 : 0.0);
        } 
        else {
            try { 
                stk.push(std::stod(tok)); 
            }
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

inline double ExpressionEvaluator::evaluate(const std::string& expr,
                                           const std::unordered_map<std::string, double>& vars) {
    auto tokens = tokenize(expr);
    auto rpn = to_rpn(tokens);
    return eval_rpn(rpn, vars);
}

// Query Executor
class QueryExecutor {
public:
    std::vector<Row> execute(const SelectQuery& query, const std::vector<Row>& data);
    void print_rows(const std::vector<Row>& rows) const;
    
private:
    ExpressionEvaluator evaluator;
};

inline std::vector<Row> QueryExecutor::execute(const SelectQuery& query, const std::vector<Row>& data) {
    std::vector<Row> result;
    
    for (const auto& row : data) {
        // WHERE filtering
        if (!query.where_clause.empty()) {
            try {
                std::unordered_map<std::string, double> vars;
                for (const auto& [k, v] : row.cols) {
                    vars[k] = row_val(row, k);
                }
                if (!evaluator.evaluate(query.where_clause, vars)) continue;
            } catch (...) {
                continue;
            }
        }
        
        // Column projection
        Row projected;
        if (query.columns.empty()) {
            projected = row;
        } else {
            for (const auto& col : query.columns) {
                auto it = row.cols.find(col);
                if (it != row.cols.end()) {
                    projected.cols[col] = it->second;
                }
            }
        }
        
        result.push_back(projected);
    }
    
    // ORDER BY
    if (!query.order_by.empty()) {
        std::sort(result.begin(), result.end(), 
            [&](const Row& a, const Row& b) {
                double va = row_val(a, query.order_by);
                double vb = row_val(b, query.order_by);
                return query.order_asc ? (va < vb) : (va > vb);
            });
    }
    
    // LIMIT
    if (query.limit >= 0 && (int)result.size() > query.limit) {
        result.resize(query.limit);
    }
    
    return result;
}

inline void QueryExecutor::print_rows(const std::vector<Row>& rows) const {
    if (rows.empty()) {
        std::cout << "(No rows)\n";
        return;
    }
    
    for (const auto& row : rows) {
        for (const auto& [k, v] : row.cols) {
            std::cout << k << "=";
            if (auto* d = std::get_if<double>(&v)) {
                std::cout << *d;
            }
            if (auto* s = std::get_if<std::string>(&v)) {
                std::cout << *s;
            }
            std::cout << "  ";
        }
        std::cout << "\n";
    }
}
