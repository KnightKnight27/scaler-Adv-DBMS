#pragma once

#include "types.h"
#include <vector>
#include <string>

class Lexer {
public:
    explicit Lexer(const std::string& sql);
    std::vector<Token> tokenize();
    
private:
    std::string input;
    size_t pos = 0;
    
    char current();
    char peek(int offset = 1);
    void advance();
    void skipWhitespace();
    
    Token readNumber();
    Token readIdentifierOrKeyword();
    Token readString();
    Token readOperator();
    
    TokenType getKeywordType(const std::string& word);
};
