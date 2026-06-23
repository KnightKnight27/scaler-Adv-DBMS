#pragma once

#include <string>

using namespace std;

namespace minidb {

enum class TokenType { KEYWORD, IDENTIFIER, NUMBER, STRING, STAR, COMMA, LPAREN, RPAREN, SEMICOLON, EQ, NE, LT, LE, GT, GE, END };

struct Token {
    TokenType type = TokenType::END;
    string text;
};

class Lexer {
public:
    explicit Lexer(const string& input);
    Token nextToken();
    Token peekToken();

private:
    string input_;
    size_t pos_ = 0;
    void skipWhitespace();
    bool atEnd() const;
    char current() const;
    void advance();
    Token readIdentifierOrKeyword();
    Token readNumber();
    Token readString();
};

}  // namespace minidb
