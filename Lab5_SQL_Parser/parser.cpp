#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <queue>
#include <stack>
#include <unordered_map>
#include <cctype>

enum class TokenType {
    IDENTIFIER,
    LITERAL,
    OP_COMPARE, 
    OP_AND,
    OP_OR,
    LPAREN,
    RPAREN,
    INVALID
};

struct Token {
    TokenType type;
    std::string value;
};

struct Row {
    std::unordered_map<std::string, int> fields;
};

std::vector<Token> Tokenize(const std::string& query) {
    std::vector<Token> tokens;
    size_t i = 0;
    while (i < query.size()) {
        if (std::isspace(query[i])) {
            i++;
            continue;
        }
        if (query[i] == '(') {
            tokens.push_back({TokenType::LPAREN, "("});
            i++;
            continue;
        }
        if (query[i] == ')') {
            tokens.push_back({TokenType::RPAREN, ")"});
            i++;
            continue;
        }
        if (query[i] == '>') {
            if (i + 1 < query.size() && query[i + 1] == '=') {
                tokens.push_back({TokenType::OP_COMPARE, ">="});
                i += 2;
            } else {
                tokens.push_back({TokenType::OP_COMPARE, ">"});
                i++;
            }
            continue;
        }
        if (query[i] == '<') {
            if (i + 1 < query.size() && query[i + 1] == '=') {
                tokens.push_back({TokenType::OP_COMPARE, "<="});
                i += 2;
            } else {
                tokens.push_back({TokenType::OP_COMPARE, "<"});
                i++;
            }
            continue;
        }
        if (query[i] == '=') {
            if (i + 1 < query.size() && query[i + 1] == '=') {
                tokens.push_back({TokenType::OP_COMPARE, "=="});
                i += 2;
            } else {
                tokens.push_back({TokenType::INVALID, "="});
                i++;
            }
            continue;
        }
        if (query[i] == '!') {
            if (i + 1 < query.size() && query[i + 1] == '=') {
                tokens.push_back({TokenType::OP_COMPARE, "!="});
                i += 2;
            } else {
                tokens.push_back({TokenType::INVALID, "!"});
                i++;
            }
            continue;
        }
        if (std::isdigit(query[i])) {
            std::string val;
            while (i < query.size() && std::isdigit(query[i])) {
                val += query[i];
                i++;
            }
            tokens.push_back({TokenType::LITERAL, val});
            continue;
        }
        if (std::isalpha(query[i])) {
            std::string val;
            while (i < query.size() && (std::isalnum(query[i]) || query[i] == '_')) {
                val += query[i];
                i++;
            }
            std::string upper_val = val;
            for (char& c : upper_val) c = std::toupper(c);
            if (upper_val == "AND") {
                tokens.push_back({TokenType::OP_AND, "AND"});
            } else if (upper_val == "OR") {
                tokens.push_back({TokenType::OP_OR, "OR"});
            } else {
                tokens.push_back({TokenType::IDENTIFIER, val});
            }
            continue;
        }
        tokens.push_back({TokenType::INVALID, std::string(1, query[i])});
        i++;
    }
    return tokens;
}

int GetPrecedence(TokenType type) {
    switch (type) {
        case TokenType::OP_OR: return 1;
        case TokenType::OP_AND: return 2;
        case TokenType::OP_COMPARE: return 3;
        default: return 0;
    }
}

std::vector<Token> ShuntingYard(const std::vector<Token>& tokens) {
    std::vector<Token> output_queue;
    std::stack<Token> operator_stack;

    for (const auto& token : tokens) {
        if (token.type == TokenType::IDENTIFIER || token.type == TokenType::LITERAL) {
            output_queue.push_back(token);
        } else if (token.type == TokenType::LPAREN) {
            operator_stack.push(token);
        } else if (token.type == TokenType::RPAREN) {
            while (!operator_stack.empty() && operator_stack.top().type != TokenType::LPAREN) {
                output_queue.push_back(operator_stack.top());
                operator_stack.pop();
            }
            if (!operator_stack.empty() && operator_stack.top().type == TokenType::LPAREN) {
                operator_stack.pop(); 
            }
        } else if (token.type == TokenType::OP_AND || token.type == TokenType::OP_OR || token.type == TokenType::OP_COMPARE) {
            while (!operator_stack.empty() && GetPrecedence(operator_stack.top().type) >= GetPrecedence(token.type)) {
                output_queue.push_back(operator_stack.top());
                operator_stack.pop();
            }
            operator_stack.push(token);
        }
    }

    while (!operator_stack.empty()) {
        output_queue.push_back(operator_stack.top());
        operator_stack.pop();
    }

    return output_queue;
}

bool EvaluateRPN(const std::vector<Token>& rpn, const Row& row) {
    std::stack<int> val_stack;

    for (const auto& token : rpn) {
        if (token.type == TokenType::LITERAL) {
            val_stack.push(std::stoi(token.value));
        } else if (token.type == TokenType::IDENTIFIER) {
            auto it = row.fields.find(token.value);
            if (it == row.fields.end()) {
                val_stack.push(0);
            } else {
                val_stack.push(it->second);
            }
        } else if (token.type == TokenType::OP_COMPARE) {
            if (val_stack.size() < 2) return false;
            int rhs = val_stack.top(); val_stack.pop();
            int lhs = val_stack.top(); val_stack.pop();
            bool res = false;
            if (token.value == ">") res = (lhs > rhs);
            else if (token.value == ">=") res = (lhs >= rhs);
            else if (token.value == "<") res = (lhs < rhs);
            else if (token.value == "<=") res = (lhs <= rhs);
            else if (token.value == "==") res = (lhs == rhs);
            else if (token.value == "!=") res = (lhs != rhs);
            val_stack.push(res ? 1 : 0);
        } else if (token.type == TokenType::OP_AND) {
            if (val_stack.size() < 2) return false;
            int rhs = val_stack.top(); val_stack.pop();
            int lhs = val_stack.top(); val_stack.pop();
            val_stack.push((lhs && rhs) ? 1 : 0);
        } else if (token.type == TokenType::OP_OR) {
            if (val_stack.size() < 2) return false;
            int rhs = val_stack.top(); val_stack.pop();
            int lhs = val_stack.top(); val_stack.pop();
            val_stack.push((lhs || rhs) ? 1 : 0);
        }
    }

    if (val_stack.empty()) return false;
    return val_stack.top() != 0;
}

int main() {
    std::cout << "=== Starting Lab 5: Shunting-Yard SQL Parser & Execution Engine ===" << std::endl;
    
    std::string query = "age > 21 AND salary >= 50000";
    std::cout << "Parsing SQL Condition: \"" << query << "\"" << std::endl;

    std::vector<Token> tokens = Tokenize(query);
    std::vector<Token> rpn = ShuntingYard(tokens);

    std::cout << "RPN Representation: ";
    for (const auto& token : rpn) {
        std::cout << token.value << " ";
    }
    std::cout << std::endl;

    std::vector<Row> dataset = {
        { {{"age", 25}, {"salary", 60000}, {"id", 1}} },
        { {{"age", 20}, {"salary", 55000}, {"id", 2}} },
        { {{"age", 30}, {"salary", 45000}, {"id", 3}} },
        { {{"age", 22}, {"salary", 50000}, {"id", 4}} }
    };

    std::cout << "\nExecuting Filter Query..." << std::endl;
    for (const auto& row : dataset) {
        bool match = EvaluateRPN(rpn, row);
        std::cout << "Row " << row.fields.at("id") 
                  << " (age: " << row.fields.at("age") 
                  << ", salary: " << row.fields.at("salary") << ") -> " 
                  << (match ? "MATCH" : "NO MATCH") << std::endl;
    }

    return 0;
}
