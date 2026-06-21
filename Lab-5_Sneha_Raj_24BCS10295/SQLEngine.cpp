#include "SQLEngine.hpp"
#include <stack>
#include <queue>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <algorithm>

int SQLEngine::GetPrecedence(const std::string& op) const {
    if (op == "OR") return 1;
    if (op == "AND") return 2;
    if (op == "=" || op == "!=" || op == "<" || op == ">") return 3;
    if (op == "+" || op == "-") return 4;
    if (op == "*" || op == "/") return 5;
    return 0;
}

bool SQLEngine::IsNumber(const std::string& token) const {
    if (token.empty()) return false;
    char* end = nullptr;
    std::strtod(token.c_str(), &end);
    return end != token.c_str() && *end == '\0';
}

bool SQLEngine::IsOperator(const std::string& token) const {
    return GetPrecedence(token) > 0;
}

std::vector<std::string> SQLEngine::Tokenize(const std::string& expr) const {
    std::vector<std::string> tokens;
    std::string token = "";
    
    for (size_t i = 0; i < expr.length(); ++i) {
        char c = expr[i];
        
        if (std::isspace(c)) {
            continue;
        }
        
        if (c == '(' || c == ')' || c == '+' || c == '-' || c == '*' || c == '/' || c == '<' || c == '>' || c == '=') {
            tokens.push_back(std::string(1, c));
        } else {
            token.clear();
            while (i < expr.length() && !std::isspace(expr[i]) && 
                   expr[i] != '(' && expr[i] != ')' && 
                   expr[i] != '+' && expr[i] != '-' && expr[i] != '*' && expr[i] != '/' &&
                   expr[i] != '<' && expr[i] != '>' && expr[i] != '=') {
                token += expr[i];
                i++;
            }
            i--;
            tokens.push_back(token);
        }
    }
    return tokens;
}

std::vector<std::string> SQLEngine::InfixToPostfix(const std::vector<std::string>& tokens) const {
    std::vector<std::string> output;
    std::stack<std::string> ops;

    for (const auto& token : tokens) {
        if (IsNumber(token) || (!IsOperator(token) && token != "(" && token != ")")) {
            output.push_back(token);
        } else if (token == "(") {
            ops.push(token);
        } else if (token == ")") {
            while (!ops.empty() && ops.top() != "(") {
                output.push_back(ops.top());
                ops.pop();
            }
            ops.pop();
        } else if (IsOperator(token)) {
            while (!ops.empty() && GetPrecedence(ops.top()) >= GetPrecedence(token)) {
                output.push_back(ops.top());
                ops.pop();
            }
            ops.push(token);
        }
    }

    while (!ops.empty()) {
        output.push_back(ops.top());
        ops.pop();
    }

    return output;
}

double SQLEngine::EvaluatePostfix(const std::vector<std::string>& postfix, const Row& row) const {
    std::stack<double> evalStack;

    for (const auto& token : postfix) {
        if (IsNumber(token)) {
            evalStack.push(std::stod(token));
        } else if (!IsOperator(token)) {
            auto it = row.find(token);
            if (it != row.end()) {
                evalStack.push(it->second);
            } else {
                throw std::runtime_error("Column not found: " + token);
            }
        } else {
            double right = evalStack.top(); evalStack.pop();
            double left = evalStack.top(); evalStack.pop();

            if (token == "+") evalStack.push(left + right);
            else if (token == "-") evalStack.push(left - right);
            else if (token == "*") evalStack.push(left * right);
            else if (token == "/") evalStack.push(left / right);
            else if (token == ">") evalStack.push(left > right ? 1.0 : 0.0);
            else if (token == "<") evalStack.push(left < right ? 1.0 : 0.0);
            else if (token == "=") evalStack.push(left == right ? 1.0 : 0.0);
            else if (token == "AND") evalStack.push((left > 0 && right > 0) ? 1.0 : 0.0);
            else if (token == "OR") evalStack.push((left > 0 || right > 0) ? 1.0 : 0.0);
        }
    }
    
    return evalStack.top();
}

void SQLEngine::ExecuteSelect(const std::string& query, const std::vector<Row>& table) const {
    std::cout << "Executing: " << query << "\n";
    std::cout << "-------------------------------------------\n";

    size_t select_pos = query.find("SELECT");
    size_t where_pos = query.find("WHERE");
    
    if (select_pos == std::string::npos || where_pos == std::string::npos) {
        std::cerr << "Syntax Error: Missing SELECT or WHERE clause.\n";
        return;
    }

    std::string cols_str = query.substr(select_pos + 6, where_pos - (select_pos + 6));
    std::vector<std::string> columns;
    std::stringstream ss(cols_str);
    std::string col;
    while (std::getline(ss, col, ',')) {
        col.erase(remove_if(col.begin(), col.end(), isspace), col.end());
        columns.push_back(col);
    }

    std::string where_expr = query.substr(where_pos + 5);

    auto tokens = Tokenize(where_expr);
    auto postfix = InfixToPostfix(tokens);

    for (const auto& c : columns) std::cout << c << "\t";
    std::cout << "\n";

    int match_count = 0;
    for (const auto& row : table) {
        if (EvaluatePostfix(postfix, row) > 0.0) {
            match_count++;
            for (const auto& c : columns) {
                if (row.find(c) != row.end()) {
                    std::cout << row.at(c) << "\t";
                } else {
                    std::cout << "NULL\t";
                }
            }
            std::cout << "\n";
        }
    }
    std::cout << "-------------------------------------------\n";
    std::cout << match_count << " rows matched.\n\n";
}