#pragma once
#include <string>
#include <vector>

namespace minidb {

enum class TokType { IDENT, NUMBER, STRING, PUNCT, END };

struct Token {
  TokType     type;
  std::string text;  // IDENT/PUNCT text, numeric literal, or string contents
};

// Tokenize a SQL statement. Identifiers and keywords are returned as IDENT
// (the parser upper-cases for keyword comparison). Strings are single-quoted.
// Punctuation tokens include multi-char operators <=, >=, !=, <>.
std::vector<Token> Tokenize(const std::string& sql);

}  // namespace minidb
