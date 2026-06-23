#include "parser/tokenizer.h"
#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <cctype>

// Keyword lookup table (case-insensitive)
static const std::unordered_map<std::string, TokenType> KEYWORDS = {
    {"select", TokenType::SELECT}, {"from", TokenType::FROM},
    {"where", TokenType::WHERE}, {"insert", TokenType::INSERT},
    {"into", TokenType::INTO}, {"values", TokenType::VALUES},
    {"delete", TokenType::DELETE}, {"update", TokenType::UPDATE},
    {"set", TokenType::SET}, {"create", TokenType::CREATE},
    {"table", TokenType::TABLE}, {"join", TokenType::JOIN},
    {"on", TokenType::ON}, {"and", TokenType::AND},
    {"or", TokenType::OR}, {"not", TokenType::NOT},
    {"primary", TokenType::PRIMARY}, {"key", TokenType::KEY},
    {"int", TokenType::INT_TYPE}, {"varchar", TokenType::VARCHAR_TYPE},
    {"bool", TokenType::BOOL_TYPE}, {"boolean", TokenType::BOOL_TYPE},
    {"true", TokenType::TRUE_LIT}, {"false", TokenType::FALSE_LIT},
    {"null", TokenType::NULL_LIT},
};

Tokenizer::Tokenizer(const std::string& input) : input_(input), pos_(0) {}

char Tokenizer::Peek() const {
    if (pos_ >= (int)input_.size()) return '\0';
    return input_[pos_];
}

char Tokenizer::Advance() {
    return input_[pos_++];
}

void Tokenizer::SkipWhitespace() {
    while (pos_ < (int)input_.size() && std::isspace(input_[pos_])) pos_++;
}

Token Tokenizer::ReadIdentifierOrKeyword() {
    int start = pos_;
    while (pos_ < (int)input_.size() &&
           (std::isalnum(input_[pos_]) || input_[pos_] == '_')) {
        pos_++;
    }
    std::string word = input_.substr(start, pos_ - start);
    std::string lower = word;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    auto it = KEYWORDS.find(lower);
    if (it != KEYWORDS.end()) {
        return {it->second, word, start};
    }
    return {TokenType::IDENTIFIER, word, start};
}

Token Tokenizer::ReadNumber() {
    int start = pos_;
    // Handle negative numbers
    if (input_[pos_] == '-') pos_++;
    while (pos_ < (int)input_.size() && std::isdigit(input_[pos_])) pos_++;
    return {TokenType::INT_LITERAL, input_.substr(start, pos_ - start), start};
}

Token Tokenizer::ReadString() {
    int start = pos_;
    pos_++;  // skip opening quote
    std::string result;
    while (pos_ < (int)input_.size() && input_[pos_] != '\'') {
        if (input_[pos_] == '\\' && pos_ + 1 < (int)input_.size()) {
            pos_++;  // skip escape
        }
        result += input_[pos_++];
    }
    if (pos_ < (int)input_.size()) pos_++;  // skip closing quote
    return {TokenType::STRING_LITERAL, result, start};
}

Token Tokenizer::ReadOperator() {
    int start = pos_;
    char c = Advance();
    switch (c) {
        case '=': return {TokenType::EQ, "=", start};
        case '<':
            if (Peek() == '=') { Advance(); return {TokenType::LTE, "<=", start}; }
            return {TokenType::LT, "<", start};
        case '>':
            if (Peek() == '=') { Advance(); return {TokenType::GTE, ">=", start}; }
            return {TokenType::GT, ">", start};
        case '!':
            if (Peek() == '=') { Advance(); return {TokenType::NEQ, "!=", start}; }
            throw std::runtime_error("Unexpected '!' at position " + std::to_string(start));
        default:
            throw std::runtime_error("Unexpected character at position " + std::to_string(start));
    }
}

std::vector<Token> Tokenizer::Tokenize() {
    std::vector<Token> tokens;

    while (pos_ < (int)input_.size()) {
        SkipWhitespace();
        if (pos_ >= (int)input_.size()) break;

        char c = Peek();
        int start = pos_;

        if (std::isalpha(c) || c == '_') {
            tokens.push_back(ReadIdentifierOrKeyword());
        } else if (std::isdigit(c)) {
            tokens.push_back(ReadNumber());
        } else if (c == '-' && pos_ + 1 < (int)input_.size() && std::isdigit(input_[pos_ + 1])) {
            tokens.push_back(ReadNumber());
        } else if (c == '\'') {
            tokens.push_back(ReadString());
        } else if (c == '(' ) { Advance(); tokens.push_back({TokenType::LPAREN, "(", start}); }
        else if (c == ')') { Advance(); tokens.push_back({TokenType::RPAREN, ")", start}); }
        else if (c == ',') { Advance(); tokens.push_back({TokenType::COMMA, ",", start}); }
        else if (c == ';') { Advance(); tokens.push_back({TokenType::SEMICOLON, ";", start}); }
        else if (c == '*') { Advance(); tokens.push_back({TokenType::STAR, "*", start}); }
        else if (c == '.') { Advance(); tokens.push_back({TokenType::DOT, ".", start}); }
        else if (c == '=' || c == '<' || c == '>' || c == '!') {
            tokens.push_back(ReadOperator());
        } else {
            throw std::runtime_error("Unexpected character '" + std::string(1, c) +
                                     "' at position " + std::to_string(pos_));
        }
    }

    tokens.push_back({TokenType::END_OF_INPUT, "", (int)input_.size()});
    return tokens;
}
