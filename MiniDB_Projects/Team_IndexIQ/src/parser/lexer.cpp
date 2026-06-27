#include "parser/lexer.h"
#include <cctype>
#include <unordered_map>

static const std::unordered_map<std::string, TokenType> KEYWORDS = {
    {"SELECT",   TokenType::SELECT},
    {"FROM",     TokenType::FROM},
    {"WHERE",    TokenType::WHERE},
    {"JOIN",     TokenType::JOIN},
    {"ON",       TokenType::ON},
    {"INSERT",   TokenType::INSERT},
    {"INTO",     TokenType::INTO},
    {"VALUES",   TokenType::VALUES},
    {"DELETE",   TokenType::DELETE},
    {"CREATE",   TokenType::CREATE},
    {"TABLE",    TokenType::TABLE},
    {"BEGIN",    TokenType::BEGIN},
    {"COMMIT",   TokenType::COMMIT},
    {"ROLLBACK", TokenType::ROLLBACK},
    {"EXPLAIN",  TokenType::EXPLAIN},
    {"AND",      TokenType::AND},
    {"OR",       TokenType::OR},
};

Lexer::Lexer(std::string sql) : input_(std::move(sql)) {}

void Lexer::skip_spaces() {
    while (pos_ < input_.size() && std::isspace(input_[pos_])) pos_++;
}

Token Lexer::read_word() {
    std::string w;
    while (pos_ < input_.size() && (std::isalnum(input_[pos_]) || input_[pos_] == '_'))
        w += input_[pos_++];
    std::string upper = w;
    for (char& c : upper) c = std::toupper(c);
    auto it = KEYWORDS.find(upper);
    if (it != KEYWORDS.end()) return {it->second, w};
    return {TokenType::IDENTIFIER, w};
}

Token Lexer::read_number() {
    std::string n;
    while (pos_ < input_.size() && std::isdigit(input_[pos_])) n += input_[pos_++];
    return {TokenType::NUMBER, n};
}

Token Lexer::read_string() {
    pos_++;
    std::string s;
    while (pos_ < input_.size() && input_[pos_] != '\'') s += input_[pos_++];
    if (pos_ < input_.size()) pos_++;
    return {TokenType::STRING, s};
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    while (true) {
        skip_spaces();
        if (pos_ >= input_.size()) break;
        char c = peek();

        if (std::isalpha(c) || c == '_') { tokens.push_back(read_word());   continue; }
        if (std::isdigit(c))             { tokens.push_back(read_number()); continue; }
        if (c == '\'')                   { tokens.push_back(read_string()); continue; }

        pos_++;
        switch (c) {
            case '*': tokens.push_back({TokenType::STAR,      "*"}); break;
            case ',': tokens.push_back({TokenType::COMMA,     ","}); break;
            case '(': tokens.push_back({TokenType::LPAREN,    "("}); break;
            case ')': tokens.push_back({TokenType::RPAREN,    ")"}); break;
            case '.': tokens.push_back({TokenType::DOT,       "."}); break;
            case ';': tokens.push_back({TokenType::SEMICOLON, ";"}); break;
            case '=': tokens.push_back({TokenType::EQ,        "="}); break;
            case '>':
                if (peek() == '=') { pos_++; tokens.push_back({TokenType::GTE, ">="}); }
                else tokens.push_back({TokenType::GT, ">"});
                break;
            case '<':
                if (peek() == '=') { pos_++; tokens.push_back({TokenType::LTE, "<="}); }
                else tokens.push_back({TokenType::LT, "<"});
                break;
            case '!':
                if (peek() == '=') { pos_++; tokens.push_back({TokenType::NEQ, "!="}); }
                break;
            default: break;
        }
    }
    tokens.push_back({TokenType::END, ""});
    return tokens;
}
