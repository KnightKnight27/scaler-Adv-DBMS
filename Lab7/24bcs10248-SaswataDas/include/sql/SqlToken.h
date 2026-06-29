#pragma once

#include <string>

namespace sql {

enum class SqlTokenType {
    Select,
    From,
    Where,
    And,
    Or,
    Identifier,
    Number,
    String,
    Comparison,  // = , != , <> , < , <= , > , >=
    Star,
    Comma,
    LeftParen,
    RightParen,
    End
};

struct SqlToken {
    SqlTokenType type;
    std::string text;     // identifier / keyword text, comparison symbol, or string body
    double number = 0.0;  // valid only when type == Number
};

}  // namespace sql
