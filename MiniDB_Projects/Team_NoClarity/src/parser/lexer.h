#ifndef LEXER_H
#define LEXER_H

#include <string>
#include <vector>

namespace minidb {

enum class TokenType {
    SELECT, FROM, WHERE, OR, AND, IDENTIFIER, NUMBER,
    GT, LT, EQUALS, LPAREN, RPAREN, COMMA, STAR, END
};

struct Token {
    TokenType type;
    std::string text;
};

class Lexer {
public:
    explicit Lexer(std::string sql);
    std::vector<Token> Tokenize();

private:
    std::string input_;
    size_t pos_{0};
};

} // namespace minidb

#endif // LEXER_H
