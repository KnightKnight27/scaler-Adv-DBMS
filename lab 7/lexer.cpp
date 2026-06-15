#include "lexer.h"
#include <cctype>
#include <stdexcept>
#include <algorithm>

Lexer::Lexer(std::string sql) : input(std::move(sql)) {}

char Lexer::peek() const {
    if (pos >= input.size()) return '\0';
    return input[pos];
}

char Lexer::advance() {
    if (pos >= input.size()) return '\0';
    return input[pos++];
}

void Lexer::skipWhitespace() {
    while (pos < input.size() && std::isspace(input[pos])) {
        pos++;
    }
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;

    while (pos < input.size()) {
        skipWhitespace();
        if (pos >= input.size()) break;

        char c = peek();

        if (std::isalpha(c) || c == '_') {
            tokens.push_back(scanWord());
        } else if (std::isdigit(c)) {
            tokens.push_back(scanNumber());
        } else if (c == '\'' || c == '"') {
            tokens.push_back(scanString());
        } else if (c == '>') {
            advance();
            if (peek() == '=') {
                advance();
                tokens.push_back({TokenType::GE, ">="});
            } else {
                tokens.push_back({TokenType::GT, ">"});
            }
        } else if (c == '<') {
            advance();
            if (peek() == '=') {
                advance();
                tokens.push_back({TokenType::LE, "<="});
            } else if (peek() == '>') {
                advance();
                tokens.push_back({TokenType::NE, "<>"});
            } else {
                tokens.push_back({TokenType::LT, "<"});
            }
        } else if (c == '=') {
            advance();
            tokens.push_back({TokenType::EQ, "="});
        } else if (c == '!') {
            advance();
            if (peek() == '=') {
                advance();
                tokens.push_back({TokenType::NE, "!="});
            } else {
                throw std::runtime_error("Unexpected operator '!'");
            }
        } else if (c == '(') {
            advance();
            tokens.push_back({TokenType::LPAREN, "("});
        } else if (c == ')') {
            advance();
            tokens.push_back({TokenType::RPAREN, ")"});
        } else if (c == ',') {
            advance();
            tokens.push_back({TokenType::COMMA, ","});
        } else if (c == '*') {
            advance();
            tokens.push_back({TokenType::STAR, "*"});
        } else {
            advance(); // Consume unrecognized character to avoid infinite loops, but throw
            throw std::runtime_error(std::string("Unrecognized character: ") + c);
        }
    }

    tokens.push_back({TokenType::END, ""});
    return tokens;
}

Token Lexer::scanWord() {
    std::string text;
    while (pos < input.size() && (std::isalnum(input[pos]) || input[pos] == '_')) {
        text += advance();
    }

    std::string upperText = text;
    std::transform(upperText.begin(), upperText.end(), upperText.begin(), ::toupper);

    if (upperText == "SELECT") return {TokenType::SELECT, text};
    if (upperText == "FROM") return {TokenType::FROM, text};
    if (upperText == "WHERE") return {TokenType::WHERE, text};
    if (upperText == "AND") return {TokenType::AND, text};
    if (upperText == "OR") return {TokenType::OR, text};
    if (upperText == "LIMIT") return {TokenType::LIMIT, text};
    if (upperText == "ORDER") return {TokenType::ORDER, text};
    if (upperText == "BY") return {TokenType::BY, text};
    if (upperText == "DESC") return {TokenType::DESC, text};
    if (upperText == "ASC") return {TokenType::ASC, text};

    return {TokenType::IDENTIFIER, text};
}

Token Lexer::scanNumber() {
    std::string text;
    bool isFloat = false;
    while (pos < input.size() && (std::isdigit(input[pos]) || input[pos] == '.')) {
        if (input[pos] == '.') {
            if (isFloat) break; // Only one decimal point allowed
            isFloat = true;
        }
        text += advance();
    }
    return {TokenType::NUMBER, text};
}

Token Lexer::scanString() {
    char quote = advance(); // Consume opening quote
    std::string text;
    while (pos < input.size() && peek() != quote) {
        text += advance();
    }
    if (pos >= input.size()) {
        throw std::runtime_error("Unterminated string literal");
    }
    advance(); // Consume closing quote
    return {TokenType::STRING, text};
}
