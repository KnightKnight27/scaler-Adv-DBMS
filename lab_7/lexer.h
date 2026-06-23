#pragma once
//
// lexer.h
// ---------------------------------------------------------------------------
// Token definitions + a hand-written lexer that turns a raw SQL string into a
// flat vector<Token>. Style mirrors the repo's existing query_parser lexer
// (single forward scan, append a final END token), but the token set is
// extended to cover arithmetic, all comparison operators, boolean AND/OR/NOT,
// string literals and the projection star.
// ---------------------------------------------------------------------------

#include <string>
#include <vector>

enum class TokenType {
    // keywords
    SELECT, FROM, WHERE,
    // logical operators
    AND, OR, NOT,
    // atoms
    IDENTIFIER,   // a column name (or table name)
    NUMBER,       // integer literal
    STRING,       // 'quoted' string literal
    // comparison operators
    EQ, NEQ, GT, LT, GTE, LTE,
    // arithmetic operators
    PLUS, MINUS, STAR, SLASH,
    // punctuation
    LPAREN, RPAREN, COMMA,
    // sentinel
    END
};

struct Token {
    TokenType type;
    std::string text;   // original text / literal contents
};

// Human-readable name for a TokenType (used in error messages & tests).
const char *tokenTypeName(TokenType t);

class Lexer {
public:
    explicit Lexer(std::string sql);
    std::vector<Token> tokenize();

private:
    std::string input;
};
