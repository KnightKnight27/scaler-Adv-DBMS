#include "lexer.h"
#include <cctype>
#include <stdexcept>
#include <algorithm>

Lexer::Lexer(std::string sql) : input(std::move(sql)) {}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    size_t pos = 0;
    size_t n = input.size();

    auto to_upper = [](std::string s) {
        for (auto& c : s) c = std::toupper(c);
        return s;
    };

    while (pos < n) {
        if (std::isspace(input[pos])) {
            pos++;
            continue;
        }

        // Single-quoted String Literal
        if (input[pos] == '\'') {
            std::string str;
            pos++; // Skip opening quote
            while (pos < n && input[pos] != '\'') {
                str += input[pos++];
            }
            if (pos >= n) {
                throw std::runtime_error("Unterminated string literal in SQL query");
            }
            pos++; // Skip closing quote
            tokens.push_back({TokenType::STRING, str});
            continue;
        }

        // Numeric literal
        if (std::isdigit(input[pos]) || (input[pos] == '.' && pos + 1 < n && std::isdigit(input[pos + 1]))) {
            std::string num;
            while (pos < n && (std::isdigit(input[pos]) || input[pos] == '.')) {
                num += input[pos++];
            }
            tokens.push_back({TokenType::NUMBER, num});
            continue;
        }

        // Identifier or Keyword
        if (std::isalpha(input[pos]) || input[pos] == '_') {
            std::string word;
            while (pos < n && (std::isalnum(input[pos]) || input[pos] == '_')) {
                word += input[pos++];
            }
            
            std::string upperWord = to_upper(word);
            if (upperWord == "SELECT") tokens.push_back({TokenType::SELECT, word});
            else if (upperWord == "FROM") tokens.push_back({TokenType::FROM, word});
            else if (upperWord == "WHERE") tokens.push_back({TokenType::WHERE, word});
            else if (upperWord == "ORDER") tokens.push_back({TokenType::ORDER, word});
            else if (upperWord == "BY") tokens.push_back({TokenType::BY, word});
            else if (upperWord == "ASC") tokens.push_back({TokenType::ASC, word});
            else if (upperWord == "DESC") tokens.push_back({TokenType::DESC, word});
            else if (upperWord == "LIMIT") tokens.push_back({TokenType::LIMIT, word});
            else if (upperWord == "AND") tokens.push_back({TokenType::AND, word});
            else if (upperWord == "OR") tokens.push_back({TokenType::OR, word});
            else if (upperWord == "NOT") tokens.push_back({TokenType::NOT, word});
            else tokens.push_back({TokenType::IDENTIFIER, word});
            continue;
        }

        // Parentheses
        if (input[pos] == '(') {
            tokens.push_back({TokenType::LPAREN, "("});
            pos++;
            continue;
        }
        if (input[pos] == ')') {
            tokens.push_back({TokenType::RPAREN, ")"});
            pos++;
            continue;
        }

        // Comma and Asterisk
        if (input[pos] == ',') {
            tokens.push_back({TokenType::COMMA, ","});
            pos++;
            continue;
        }
        if (input[pos] == '*') {
            tokens.push_back({TokenType::ASTERISK, "*"});
            pos++;
            continue;
        }

        // Operators
        if (pos + 1 < n) {
            std::string two = input.substr(pos, 2);
            if (two == ">=") {
                tokens.push_back({TokenType::GTE, ">="});
                pos += 2;
                continue;
            }
            if (two == "<=") {
                tokens.push_back({TokenType::LTE, "<="});
                pos += 2;
                continue;
            }
            if (two == "!=") {
                tokens.push_back({TokenType::NEQ, "!="});
                pos += 2;
                continue;
            }
            if (two == "<>") {
                tokens.push_back({TokenType::NEQ, "<>"});
                pos += 2;
                continue;
            }
            if (two == "&&") {
                tokens.push_back({TokenType::AND, "&&"});
                pos += 2;
                continue;
            }
            if (two == "||") {
                tokens.push_back({TokenType::OR, "||"});
                pos += 2;
                continue;
            }
        }

        char one = input[pos];
        if (one == '>') {
            tokens.push_back({TokenType::GT, ">"});
            pos++;
        } else if (one == '<') {
            tokens.push_back({TokenType::LT, "<"});
            pos++;
        } else if (one == '=') {
            tokens.push_back({TokenType::EQ, "="});
            pos++;
        } else if (one == '+' || one == '-' || one == '/' || one == '^') {
            tokens.push_back({TokenType::OPERATOR, std::string(1, one)});
            pos++;
        } else {
            // Unknown character, skip it
            pos++;
        }
    }

    tokens.push_back({TokenType::END, ""});
    return tokens;
}
