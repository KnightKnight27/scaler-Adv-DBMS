#include "SQLEngine.hpp"
#include <stack>
#include <queue>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <algorithm>


// Operator Precedence & Token Identification

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


// Parsing Pipeline: Tokenizer & Shunting-Yard Transformer

std::vector<std::string> SQLEngine::Tokenize(const std::string& expr) const {
    std::vector<std::string> tokens;
    std::string token = "";
    
    for (size_t i = 0; i < expr.length(); ++i) {
        char c = expr[i];
        
        if (std::isspace(c)) {
            continue;
        }
        
        // Isolate single-character operators and structural symbols
        if (c == '(' || c == ')' || c == '+' || c == '-' || c == '*' || c == '/' || c == '<' || c == '>' || c == '=') {
            tokens.push_back(std::string(1, c));
        } else {
            // Group multi-character identifiers, words, and constants
            token.clear();
            while (i < expr.length() && !std::isspace(expr[i]) && 
                   expr[i] != '(' && expr[i] != ')' && 
                   expr[i] != '+' && expr[i] != '-' && expr[i] != '*' && expr[i] != '/' &&
                   expr[i] != '<' && expr[i] != '>' && expr[i] != '=') {
                token += expr[i];
                i++;
            }
            i--; // Rewind pointer to compensate for lookahead breach
            tokens.push_back(token);
        }
    }
    return tokens;
}

std::vector<std::string> SQLEngine::InfixToPostfix(const std::vector<std::string>& tokens) const {
    std::vector<std::string> output;
    std::stack<std::string> ops;

    for (const auto& token : tokens) {
        // Condition A: Token is an operand (Literal value or Database column identifier)
        if (IsNumber(token) || (!IsOperator(token) && token != "(" && token != ")")) {
            output.push_back(token);
        } 
        // Condition B: Left parenthesis opening a nested expression scope
        else if (token == "(") {
            ops.push(token);
        } 
        // Condition C: Right parenthesis closing a nested expression scope
        else if (token == ")") {
            while (!ops.empty() && ops.top() != "(") {
                output.push_back(ops.top());
                ops.pop();
            }
            if (!ops.empty()) {
                ops.pop(); // Discard matching '(' token from stack
            }
        } 
        // Condition D: Token is a logical or mathematical operator
        else if (IsOperator(token)) {
            while (!ops.empty() && GetPrecedence(ops.top()) >= GetPrecedence(token)) {
                output.push_back(ops.top());
                ops.pop();
            }
            ops.push(token);
        }
    }

    // Flush any remaining active operators to the output vector
    while (!ops.empty()) {
        output.push_back(ops.top());
        ops.pop();
    }

    return output;
}


// Postfix Evaluation Engine

double SQLEngine::EvaluatePostfix(const std::vector<std::string>& postfix, const Row& row) const {
    std::stack<double> evalStack;

    for (const auto& token : postfix) {
        if (IsNumber(token)) {
            evalStack.push(std::stod(token));
        } 
        else if (!IsOperator(token)) {
            // Contextually resolve the database identifier name using row schema values
            auto it = row.find(token);
            if (it != row.end()) {
                evalStack.push(it->second);
            } else {
                throw std::runtime_error("Database Schema Error: Column '" + token + "' not found.");
            }
        } 
        else {
            // Pop trailing structural right-hand element followed by left-hand element
            double right = evalStack.top(); evalStack.pop();
            double left = evalStack.top();  evalStack.pop();

            if (token == "+")      evalStack.push(left + right);
            else if (token == "-") evalStack.push(left - right);
            else if (token == "*") evalStack.push(left * right);
            else if (token == "/") evalStack.push(left / right);
            else if (token == ">") evalStack.push(left > right ? 1.0 : 0.0);
            else if (token == "<") evalStack.push(left < right ? 1.0 : 0.0);
            else if (token == "=") evalStack.push(left == right ? 1.0 : 0.0);
            else if (token == "AND") evalStack.push((left > 0.0 && right > 0.0) ? 1.0 : 0.0);
            else if (token == "OR")  evalStack.push((left > 0.0 || right > 0.0) ? 1.0 : 0.0);
        }
    }
    
    return evalStack.empty() ? 0.0 : evalStack.top();
}


// Query Execution Entry Point

void SQLEngine::ExecuteSelect(const std::string& query, const std::vector<Row>& table) const {
    std::cout << "Executing: " << query << "\n";
    std::cout << "\n";

    size_t select_pos = query.find("SELECT");
    size_t where_pos = query.find("WHERE");
    
    if (select_pos == std::string::npos || where_pos == std::string::npos) {
        std::cerr << "Syntax Error: Missing SELECT or WHERE clause.\n";
        return;
    }

    // Isolate target query projection columns
    std::string cols_str = query.substr(select_pos + 6, where_pos - (select_pos + 6));
    std::vector<std::string> columns;
    std::stringstream ss(cols_str);
    std::string col;
    
    while (std::getline(ss, col, ',')) {
        col.erase(std::remove_if(col.begin(), col.end(), ::isspace), col.end());
        columns.push_back(col);
    }

    // Isolate lookup evaluation expressions
    std::string where_expr = query.substr(where_pos + 5);
    auto tokens = Tokenize(where_expr);
    auto postfix = InfixToPostfix(tokens);

    // Print out matching structural layout headers
    for (const auto& c : columns) {
        std::cout << c << "\t";
    }
    std::cout << "\n";

    // Scan table storage records sequentially
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
    
    std::cout << "\n";
    std::cout << match_count << " rows matched.\n\n";
}