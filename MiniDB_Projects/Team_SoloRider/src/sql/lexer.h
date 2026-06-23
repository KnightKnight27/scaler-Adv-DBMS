#pragma once
// sql/lexer.h — SQL tokenizer interface.
//
// The Lexer takes a raw SQL string and chops it into a flat list of Tokens.
// Each Token carries its type, the matched text, and the character position
// in the original string (handy for error messages).

#include <string>
#include <vector>

namespace minidb {

// ─── Token Types ─────────────────────────────────────────────
enum class TokenType {
    // Keywords
    SELECT, FROM, WHERE, INSERT, INTO, VALUES, DELETE,
    CREATE, TABLE, JOIN, INNER, LEFT, ON, AND, OR, NOT,
    PRIMARY, KEY,
    INT_TYPE, FLOAT_TYPE, VARCHAR_TYPE, BOOL_TYPE,
    TRUE_KW, FALSE_KW, NULL_KW,
    AS,

    // Symbols
    STAR, COMMA, LPAREN, RPAREN, SEMICOLON, DOT,

    // Comparison operators
    EQ, NEQ, LT, GT, LTE, GTE,

    // Literals
    INTEGER_LIT, FLOAT_LIT, STRING_LIT,

    // Identifiers
    IDENTIFIER,

    // Sentinels
    END_OF_INPUT,
    INVALID
};

// ─── Token ───────────────────────────────────────────────────
struct Token {
    TokenType type;
    std::string value;
    int position;  // Character offset in the original input.
};

// ─── Lexer ───────────────────────────────────────────────────
class Lexer {
public:
    /// Construct a lexer for the given SQL input string.
    explicit Lexer(const std::string& input);

    /// Scan the input and produce every token up to END_OF_INPUT.
    std::vector<Token> tokenize();

private:
    std::string input_;
    size_t pos_;

    // Character-level helpers.
    char peek() const;
    char advance();

    // Whitespace / comment skipping.
    void skip_whitespace();

    // Sub-scanners.
    Token read_identifier_or_keyword();
    Token read_number();
    Token read_string_literal();
};

}  // namespace minidb
