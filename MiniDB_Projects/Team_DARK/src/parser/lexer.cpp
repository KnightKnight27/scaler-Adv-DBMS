#include "parser/lexer.h"

#include <cctype>

namespace minidb {

Lexer::Lexer(std::string sql) : input_(std::move(sql)) {}

std::vector<Token> Lexer::Tokenize() {
    std::vector<Token> tokens;
    std::size_t pos = 0;

    while (pos < input_.size()) {
        if (std::isspace(static_cast<unsigned char>(input_[pos]))) {
            ++pos;
            continue;
        }

        if (std::isalpha(static_cast<unsigned char>(input_[pos]))) {
            std::string word;
            while (pos < input_.size() &&
                   (std::isalnum(static_cast<unsigned char>(input_[pos])) ||
                    input_[pos] == '_')) {
                word += input_[pos++];
            }

            if (word == "SELECT") {
                tokens.push_back({TokenType::SELECT, word});
            } else if (word == "FROM") {
                tokens.push_back({TokenType::FROM, word});
            } else if (word == "WHERE") {
                tokens.push_back({TokenType::WHERE, word});
            } else if (word == "INSERT") {
                tokens.push_back({TokenType::INSERT, word});
            } else if (word == "INTO") {
                tokens.push_back({TokenType::INTO, word});
            } else if (word == "DELETE") {
                tokens.push_back({TokenType::DELETE, word});
            } else if (word == "VALUES") {
                tokens.push_back({TokenType::VALUES, word});
            } else if (word == "JOIN") {
                tokens.push_back({TokenType::JOIN, word});
            } else if (word == "ON") {
                tokens.push_back({TokenType::ON, word});
            } else if (word == "AND") {
                tokens.push_back({TokenType::AND, word});
            } else if (word == "OR") {
                tokens.push_back({TokenType::OR, word});
            } else {
                tokens.push_back({TokenType::IDENTIFIER, word});
            }
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(input_[pos]))) {
            std::string num;
            while (pos < input_.size() &&
                   std::isdigit(static_cast<unsigned char>(input_[pos]))) {
                num += input_[pos++];
            }
            tokens.push_back({TokenType::NUMBER, num});
            continue;
        }

        if (input_[pos] == '\'') {
            ++pos;
            std::string str;
            while (pos < input_.size() && input_[pos] != '\'') {
                str += input_[pos++];
            }
            if (pos < input_.size() && input_[pos] == '\'') {
                ++pos;
            }
            tokens.push_back({TokenType::STRING, str});
            continue;
        }

        if (input_[pos] == '>') {
            tokens.push_back({TokenType::GT, ">"});
            ++pos;
            continue;
        }
        if (input_[pos] == '<') {
            tokens.push_back({TokenType::LT, "<"});
            ++pos;
            continue;
        }
        if (input_[pos] == '=') {
            tokens.push_back({TokenType::EQ, "="});
            ++pos;
            continue;
        }
        if (input_[pos] == ',') {
            tokens.push_back({TokenType::COMMA, ","});
            ++pos;
            continue;
        }
        if (input_[pos] == '(') {
            tokens.push_back({TokenType::LPAREN, "("});
            ++pos;
            continue;
        }
        if (input_[pos] == ')') {
            tokens.push_back({TokenType::RPAREN, ")"});
            ++pos;
            continue;
        }

        ++pos;
    }

    tokens.push_back({TokenType::END, ""});
    return tokens;
}

}  // namespace minidb
