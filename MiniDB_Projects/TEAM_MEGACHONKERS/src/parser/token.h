#pragma once

#include <string>

namespace minidb {

// The complete vocabulary the lexer can emit. Keywords get dedicated token
// types so the recursive-descent parser can match on them directly instead of
// re-comparing raw strings on every production.
enum class TokenType {
    // Literals & identifiers
    IDENTIFIER,
    INTEGER_LITERAL,
    STRING_LITERAL,

    // Keywords
    KW_SELECT,
    KW_FROM,
    KW_WHERE,
    KW_INSERT,
    KW_INTO,
    KW_VALUES,
    KW_CREATE,
    KW_TABLE,
    KW_DELETE,
    KW_JOIN,
    KW_ON,
    KW_AND,
    KW_OR,
    KW_NOT,
    KW_BEGIN,
    KW_COMMIT,
    KW_ROLLBACK,
    KW_INDEX,
    KW_INT,
    KW_INTEGER,
    KW_VARCHAR,
    KW_EXIT,

    // Operators
    EQUAL,         // =
    NOT_EQUAL,     // != or <>
    LESS,          // <
    LESS_EQUAL,    // <=
    GREATER,       // >
    GREATER_EQUAL, // >=

    // Punctuation
    LPAREN,        // (
    RPAREN,        // )
    COMMA,         // ,
    SEMICOLON,     // ;
    DOT,           // .
    STAR,          // *

    END_OF_FILE,
    INVALID
};

struct Token {
    TokenType type{TokenType::INVALID};
    std::string lexeme;     // The raw text (identifiers/literals keep their value)
    size_t position{0};     // Offset into the source, useful for diagnostics

    Token() = default;
    Token(TokenType t, std::string lex, size_t pos = 0)
        : type(t), lexeme(std::move(lex)), position(pos) {}

    bool Is(TokenType t) const { return type == t; }
};

} // namespace minidb
