#pragma once

#include <string>
#include <vector>

namespace minidb {

enum class TokenType {
    SELECT,
    FROM,
    WHERE,
    INSERT,
    INTO,
    DELETE,
    VALUES,
    JOIN,
    ON,
    AND,
    OR,
    IDENTIFIER,
    NUMBER,
    STRING,
    GT,
    LT,
    EQ,
    COMMA,
    LPAREN,
    RPAREN,
    END
};

struct Token {
    TokenType type = TokenType::END;
    std::string text;
};

class Lexer {
public:
    explicit Lexer(std::string sql);

    std::vector<Token> Tokenize();

private:
    std::string input_;
};

}  // namespace minidb
