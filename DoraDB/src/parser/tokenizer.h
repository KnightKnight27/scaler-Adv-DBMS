#pragma once

#include <string>
#include <vector>

// ============================================================
// Token — one piece of a SQL statement
// ============================================================

enum class TokenType {
    // Keywords
    SELECT, FROM, WHERE, INSERT, INTO, VALUES, DELETE, UPDATE, SET,
    CREATE, TABLE, JOIN, ON, AND, OR, NOT,
    PRIMARY, KEY,
    INT_TYPE, VARCHAR_TYPE, BOOL_TYPE,
    TRUE_LIT, FALSE_LIT, NULL_LIT,

    // Identifiers & literals
    IDENTIFIER,     // table/column names
    INT_LITERAL,    // 42
    STRING_LITERAL, // 'hello'

    // Operators
    EQ,      // =
    NEQ,     // !=
    LT,      // <
    GT,      // >
    LTE,     // <=
    GTE,     // >=

    // Punctuation
    LPAREN,    // (
    RPAREN,    // )
    COMMA,     // ,
    SEMICOLON, // ;
    STAR,      // *
    DOT,       // .

    // End of input
    END_OF_INPUT
};

struct Token {
    TokenType type;
    std::string value;  // raw text of the token
    int position;       // character position in input (for error messages)
};

// ============================================================
// Tokenizer — splits a SQL string into tokens
// ============================================================

class Tokenizer {
public:
    explicit Tokenizer(const std::string& input);

    // Get all tokens at once
    std::vector<Token> Tokenize();

private:
    std::string input_;
    int pos_;

    char Peek() const;
    char Advance();
    void SkipWhitespace();

    Token ReadIdentifierOrKeyword();
    Token ReadNumber();
    Token ReadString();
    Token ReadOperator();
};
