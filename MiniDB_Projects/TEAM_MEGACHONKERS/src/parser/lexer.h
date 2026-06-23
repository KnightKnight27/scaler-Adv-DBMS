#pragma once

#include <string>
#include <vector>
#include "parser/token.h"

namespace minidb {

// Converts a raw SQL string into a flat stream of Tokens. The lexer is the
// single source of truth for "what is a keyword", "what is an operator", and
// how string/integer literals are delimited -- the parser never re-scans
// characters. This is the lexing half of the Lexer/AST refactor: the old
// engine split on whitespace and re-classified strings everywhere it needed
// them; now classification happens exactly once, here.
class Lexer {
public:
    explicit Lexer(std::string source) : source_(std::move(source)) {}

    // Tokenizes the entire input. The final token is always END_OF_FILE so the
    // parser can look ahead without bounds-checking. An INVALID token carries
    // the offending text in its lexeme for error reporting.
    std::vector<Token> Tokenize();

private:
    std::string source_;
    size_t pos_{0};

    char Peek() const;
    char PeekNext() const;
    char Advance();
    bool AtEnd() const;

    Token LexIdentifierOrKeyword();
    Token LexNumber();
    Token LexString(char quote);
    Token LexOperatorOrPunct();

    // Maps an UPPER-CASED identifier to its keyword TokenType, or
    // TokenType::IDENTIFIER if it is not a reserved word.
    static TokenType KeywordType(const std::string& upper);
};

} // namespace minidb
