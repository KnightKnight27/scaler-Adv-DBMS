#pragma once
#include "parser/types.h"
#include <string>
#include <vector>

class Lexer {
public:
    explicit Lexer(std::string sql);
    std::vector<Token> tokenize();

private:
    std::string input_;
    size_t      pos_ = 0;

    char    peek()    const { return pos_ < input_.size() ? input_[pos_] : '\0'; }
    char    advance()       { return input_[pos_++]; }
    void    skip_spaces();
    Token   read_word();
    Token   read_number();
    Token   read_string();
};
