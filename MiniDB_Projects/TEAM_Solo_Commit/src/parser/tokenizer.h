// MiniDB - hand-written lexer (the Lab 5 tokenizer, extended to full statements).
// Splits a SQL string into keywords, identifiers, numbers, string literals, and punctuation.
#pragma once

#include <string>
#include <vector>

namespace minidb {

enum class TokKind { Keyword, Ident, Number, String, Op, Punct, End };

struct Token {
    TokKind kind;
    std::string text;   // for keywords, the UPPERCASED form
    std::string raw;    // original spelling (identifiers, string contents)
};

// Tokenize the whole input. Throws std::runtime_error on an illegal character.
std::vector<Token> Tokenize(const std::string& sql);

}  // namespace minidb
