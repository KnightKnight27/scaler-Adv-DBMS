#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <unordered_map>
#include <stack>
#include <algorithm>
#include <cctype>

using Row = std::unordered_map<std::string, std::string>;

struct Token {
    enum Type { IDENTIFIER, NUMBER, STRING, OPERATOR, LPAREN, RPAREN, KEYWORD };
    Type type;
    std::string value;
};

std::vector<Token> tokenize(const std::string& sql) {
    std::vector<Token> tokens;
    size_t i = 0;
    while (i < sql.length()) {
        if (std::isspace(sql[i])) {
            i++;
            continue;
        }
        if (sql[i] == '(') {
            tokens.push_back({Token::LPAREN, "("});
            i++;
            continue;
        }
        if (sql[i] == ')') {
            tokens.push_back({Token::RPAREN, ")"});
            i++;
            continue;
        }
        if (sql[i] == '=' || sql[i] == '<' || sql[i] == '>') {
            std::string op(1, sql[i]);
            if (i + 1 < sql.length() && sql[i + 1] == '=') {
                op += '=';
                i++;
            }
            tokens.push_back({Token::OPERATOR, op});
            i++;
            continue;
        }
        if (sql[i] == '\'') {
            std::string str;
            i++;
            while (i < sql.length() && sql[i] != '\'') {
                str += sql[i];
                i++;
            }
            i++;
            tokens.push_back({Token::STRING, str});
            continue;
        }
        if (std::isdigit(sql[i])) {
            std::string num;
            while (i < sql.length() && (std::isdigit(sql[i]) || sql[i] == '.')) {
                num += sql[i];
                i++;
            }
            tokens.push_back({Token::NUMBER, num});
            continue;
        }
        if (std::isalpha(sql[i])) {
            std::string ident;
            while (i < sql.length() && (std::isalnum(sql[i]) || sql[i] == '_')) {
                ident += sql[i];
                i++;
            }
            std::string upperIdent = ident;
            std::transform(upperIdent.begin(), upperIdent.end(), upperIdent.begin(), ::toupper);
            
            if (upperIdent == "SELECT" || upperIdent == "FROM" || upperIdent == "WHERE" || upperIdent == "AND" || upperIdent == "OR") {
                tokens.push_back({Token::KEYWORD, upperIdent});
            } else {
                tokens.push_back({Token::IDENTIFIER, ident});
            }
            continue;
        }
        i++;
    }
    return tokens;
}

int getPrecedence(const std::string& op) {
    if (op == "OR") return 1;
    if (op == "AND") return 2;
    if (op == "=" || op == "<" || op == ">" || op == "<=" || op == ">=") return 3;
    return 0;
}

std::vector<Token> shuntingYard(const std::vector<Token>& whereTokens) {
    std::vector<Token> output;
    std::stack<Token> opStack;

    for (const auto& token : whereTokens) {
        if (token.type == Token::IDENTIFIER || token.type == Token::NUMBER || token.type == Token::STRING) {
            output.push_back(token);
        } else if (token.type == Token::KEYWORD || token.type == Token::OPERATOR) {
            while (!opStack.empty() && opStack.top().type != Token::LPAREN &&
                   getPrecedence(opStack.top().value) >= getPrecedence(token.value)) {
                output.push_back(opStack.top());
                opStack.pop();
            }
            opStack.push(token);
        } else if (token.type == Token::LPAREN) {
            opStack.push(token);
        } else if (token.type == Token::RPAREN) {
            while (!opStack.empty() && opStack.top().type != Token::LPAREN) {
                output.push_back(opStack.top());
                opStack.pop();
            }
            if (!opStack.empty()) opStack.pop();
        }
    }
    while (!opStack.empty()) {
        output.push_back(opStack.top());
        opStack.pop();
    }
    return output;
}

bool evaluateRPN(const std::vector<Token>& rpn, const Row& row) {
    if (rpn.empty()) return true;
    std::stack<std::string> evalStack;

    for (const auto& token : rpn) {
        if (token.type == Token::NUMBER || token.type == Token::STRING) {
            evalStack.push(token.value);
        } else if (token.type == Token::IDENTIFIER) {
            evalStack.push(row.at(token.value));
        } else if (token.type == Token::OPERATOR || token.type == Token::KEYWORD) {
            std::string right = evalStack.top(); evalStack.pop();
            std::string left = evalStack.top(); evalStack.pop();

            if (token.value == "=") {
                evalStack.push(left == right ? "1" : "0");
            } else if (token.value == ">") {
                evalStack.push(std::stod(left) > std::stod(right) ? "1" : "0");
            } else if (token.value == "<") {
                evalStack.push(std::stod(left) < std::stod(right) ? "1" : "0");
            } else if (token.value == "AND") {
                evalStack.push((left == "1" && right == "1") ? "1" : "0");
            } else if (token.value == "OR") {
                evalStack.push((left == "1" || right == "1") ? "1" : "0");
            }
        }
    }
    return evalStack.top() == "1";
}

void executeSelect(const std::string& query, const std::vector<Row>& table) {
    auto tokens = tokenize(query);
    std::vector<std::string> projectionColumns;
    std::vector<Token> whereTokens;
    
    size_t i = 0;
    if (tokens[i].value == "SELECT") i++;
    
    while (i < tokens.size() && tokens[i].value != "FROM") {
        if (tokens[i].type == Token::IDENTIFIER) {
            projectionColumns.push_back(tokens[i].value);
        }
        i++;
    }
    
    while (i < tokens.size() && tokens[i].value != "WHERE") {
        i++;
    }
    
    if (i < tokens.size() && tokens[i].value == "WHERE") {
        i++;
        while (i < tokens.size()) {
            whereTokens.push_back(tokens[i]);
            i++;
        }
    }

    auto rpn = shuntingYard(whereTokens);

    for (const auto& col : projectionColumns) {
        std::cout << col << "\t";
    }
    std::cout << "\n---------------------\n";

    for (const auto& row : table) {
        if (evaluateRPN(rpn, row)) {
            for (const auto& col : projectionColumns) {
                std::cout << row.at(col) << "\t";
            }
            std::cout << "\n";
        }
    }
}

int main() {
    std::vector<Row> students = {
        {{"id", "1"}, {"name", "Alice"}, {"gpa", "3.8"}},
        {{"id", "2"}, {"name", "Bob"}, {"gpa", "3.5"}},
        {{"id", "3"}, {"name", "Charlie"}, {"gpa", "3.9"}}
    };

    std::string query = "SELECT name, gpa FROM students WHERE gpa > 3.6 AND id = 3";
    std::cout << "Query: " << query << "\n\n";
    executeSelect(query, students);

    return 0;
}
