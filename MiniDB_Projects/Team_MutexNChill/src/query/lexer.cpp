#include "lexer.h"
#include <cctype>
#include <stdexcept>

Lexer::Lexer(const std::string& sql) : input(sql) {}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    size_t pos = 0;

    while (pos < input.size()) {
        // Skip whitespace
        if (isspace(input[pos])) { pos++; continue; }

        // Keywords and identifiers (start with a letter or underscore)
        if (isalpha(input[pos]) || input[pos] == '_') {
            std::string word;
            while (pos < input.size() && (isalnum(input[pos]) || input[pos] == '_'))
                word += input[pos++];

            // Check if it's a keyword (case-sensitive match, uppercase expected)
            if      (word == "SELECT") tokens.push_back({TokenType::SELECT,     word});
            else if (word == "FROM")   tokens.push_back({TokenType::FROM,       word});
            else if (word == "WHERE")  tokens.push_back({TokenType::WHERE,      word});
            else if (word == "INSERT") tokens.push_back({TokenType::INSERT,     word});
            else if (word == "INTO")   tokens.push_back({TokenType::INTO,       word});
            else if (word == "VALUES") tokens.push_back({TokenType::VALUES,     word});
            else if (word == "DELETE") tokens.push_back({TokenType::DELETE,     word});
            else if (word == "JOIN")   tokens.push_back({TokenType::JOIN,       word});
            else if (word == "ON")     tokens.push_back({TokenType::ON,         word});
            else if (word == "AND")    tokens.push_back({TokenType::AND,        word});
            else if (word == "OR")     tokens.push_back({TokenType::OR,         word});
            else                       tokens.push_back({TokenType::IDENTIFIER, word});
            continue;
        }

        // Integer literals
        if (isdigit(input[pos])) {
            std::string num;
            while (pos < input.size() && isdigit(input[pos])) num += input[pos++];
            tokens.push_back({TokenType::NUMBER, num});
            continue;
        }

        // String literals (single-quoted)
        if (input[pos] == '\'') {
            pos++; // skip opening quote
            std::string s;
            while (pos < input.size() && input[pos] != '\'') s += input[pos++];
            if (pos < input.size()) pos++; // skip closing quote
            tokens.push_back({TokenType::STRING, s});
            continue;
        }

        // Two-character operators
        if (pos + 1 < input.size()) {
            std::string two = input.substr(pos, 2);
            if      (two == "!=") { tokens.push_back({TokenType::NEQ, two}); pos += 2; continue; }
            else if (two == ">=") { tokens.push_back({TokenType::GTE, two}); pos += 2; continue; }
            else if (two == "<=") { tokens.push_back({TokenType::LTE, two}); pos += 2; continue; }
        }

        // Single-character tokens
        char c = input[pos++];
        switch (c) {
            case '=': tokens.push_back({TokenType::EQ,     "="}); break;
            case '>': tokens.push_back({TokenType::GT,     ">"}); break;
            case '<': tokens.push_back({TokenType::LT,     "<"}); break;
            case '(': tokens.push_back({TokenType::LPAREN, "("}); break;
            case ')': tokens.push_back({TokenType::RPAREN, ")"}); break;
            case ',': tokens.push_back({TokenType::COMMA,  ","}); break;
            case '*': tokens.push_back({TokenType::STAR,   "*"}); break;
            case '.': tokens.push_back({TokenType::DOT,    "."}); break;
            default:  break; // ignore unknown characters
        }
    }

    tokens.push_back({TokenType::END, ""});
    return tokens;
}
