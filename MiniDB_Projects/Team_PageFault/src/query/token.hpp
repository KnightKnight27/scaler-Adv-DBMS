#pragma once
#include <string>
#include <cstdint>

namespace minidb {

enum class Tok {
    END,
    IDENT, INT_LIT, STR_LIT,
    LPAREN, RPAREN, COMMA, SEMI, STAR, DOT,
    EQ, NE, LT, LE, GT, GE,
    // keywords
    CREATE, TABLE, INSERT, INTO, VALUES, SELECT, FROM, WHERE, AND, OR,
    DELETE, JOIN, ON, BEGIN, COMMIT, ROLLBACK, COUNT, KW_INT, KW_TEXT,
};

struct Token {
    Tok type = Tok::END;
    std::string text;     // identifier / string literal value
    int64_t ival = 0;     // integer literal value
};

} // namespace minidb
