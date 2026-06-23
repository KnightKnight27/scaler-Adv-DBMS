#include "parser/Lexer.hpp"

#include <cctype>

using namespace std;

namespace minidb {

Lexer::Lexer(const string& input) : input_(input) {}

bool Lexer::atEnd() const { return pos_ >= input_.size(); }
char Lexer::current() const { return atEnd() ? '\0' : input_[pos_]; }
void Lexer::advance() { if (!atEnd()) ++pos_; }

void Lexer::skipWhitespace() {
    while (!atEnd() && isspace((unsigned char)current())) advance();
}

Token Lexer::readIdentifierOrKeyword() {
    size_t start = pos_;
    while (!atEnd() && (isalnum((unsigned char)current()) || current() == '_')) advance();
    return Token{TokenType::KEYWORD, input_.substr(start, pos_ - start)};
}

Token Lexer::readNumber() {
    size_t start = pos_;
    while (!atEnd() && isdigit((unsigned char)current())) advance();
    return Token{TokenType::NUMBER, input_.substr(start, pos_ - start)};
}

Token Lexer::readString() {
    advance();
    size_t start = pos_;
    while (!atEnd() && current() != '\'') advance();
    string val = input_.substr(start, pos_ - start);
    if (!atEnd()) advance();
    return Token{TokenType::STRING, val};
}

Token Lexer::nextToken() {
    skipWhitespace();
    if (atEnd()) return Token{TokenType::END, ""};
    char c = current();
    if (isalpha((unsigned char)c) || c == '_') return readIdentifierOrKeyword();
    if (isdigit((unsigned char)c)) return readNumber();
    if (c == '\'') return readString();
    advance();
    switch (c) {
        case '*': return Token{TokenType::STAR, "*"};
        case ',': return Token{TokenType::COMMA, ","};
        case '(': return Token{TokenType::LPAREN, "("};
        case ')': return Token{TokenType::RPAREN, ")"};
        case ';': return Token{TokenType::SEMICOLON, ";"};
        case '=': return Token{TokenType::EQ, "="};
        case '<':
            if (current() == '=') { advance(); return Token{TokenType::LE, "<="}; }
            if (current() == '>') { advance(); return Token{TokenType::NE, "<>"}; }
            return Token{TokenType::LT, "<"};
        case '>':
            if (current() == '=') { advance(); return Token{TokenType::GE, ">="}; }
            return Token{TokenType::GT, ">"};
        default: return Token{TokenType::END, ""};
    }
}

Token Lexer::peekToken() {
    size_t saved = pos_;
    Token t = nextToken();
    pos_ = saved;
    return t;
}

}  // namespace minidb
