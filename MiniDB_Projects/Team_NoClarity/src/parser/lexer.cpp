#include "parser/lexer.h"
#include <cctype>
#include <algorithm>

namespace minidb {

Lexer::Lexer(std::string sql) : input_(std::move(sql)) {}

std::vector<Token> Lexer::Tokenize() {
    std::vector<Token> tokens;
    pos_ = 0;

    while (pos_ < input_.size()) {
        char curr = input_[pos_];

        if (std::isspace(curr)) {
            pos_++;
            continue;
        }

        if (std::isalpha(curr) || curr == '_') {
            std::string word;
            while (pos_ < input_.size() && (std::isalnum(input_[pos_]) || input_[pos_] == '_')) {
                word += input_[pos_++];
            }

            // Convert to uppercase to make SQL keywords case-insensitive
            std::string upper_word = word;
            std::transform(upper_word.begin(), upper_word.end(), upper_word.begin(), ::toupper);

            if (upper_word == "SELECT") {
                tokens.push_back({TokenType::SELECT, word});
            } else if (upper_word == "FROM") {
                tokens.push_back({TokenType::FROM, word});
            } else if (upper_word == "WHERE") {
                tokens.push_back({TokenType::WHERE, word});
            } else if (upper_word == "OR") {
                tokens.push_back({TokenType::OR, word});
            } else if (upper_word == "AND") {
                tokens.push_back({TokenType::AND, word});
            } else {
                tokens.push_back({TokenType::IDENTIFIER, word});
            }
        } else if (std::isdigit(curr)) {
            std::string num;
            while (pos_ < input_.size() && std::isdigit(input_[pos_])) {
                num += input_[pos_++];
            }
            tokens.push_back({TokenType::NUMBER, num});
        } else if (curr == '=') {
            tokens.push_back({TokenType::EQUALS, "="});
            pos_++;
        } else if (curr == '>') {
            tokens.push_back({TokenType::GT, ">"});
            pos_++;
        } else if (curr == '<') {
            tokens.push_back({TokenType::LT, "<"});
            pos_++;
        } else if (curr == '(') {
            tokens.push_back({TokenType::LPAREN, "("});
            pos_++;
        } else if (curr == ')') {
            tokens.push_back({TokenType::RPAREN, ")"});
            pos_++;
        } else if (curr == ',') {
            tokens.push_back({TokenType::COMMA, ","});
            pos_++;
        } else if (curr == '*') {
            tokens.push_back({TokenType::STAR, "*"});
            pos_++;
        } else {
            // Skip unknown symbols
            pos_++;
        }
    }

    tokens.push_back({TokenType::END, ""});
    return tokens;
}

} // namespace minidb
