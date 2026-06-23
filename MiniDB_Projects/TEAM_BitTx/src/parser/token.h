#pragma once

#include <string>

namespace minidb {

using namespace std;

enum class TokenType {
  KEYWORD,
  IDENT,
  NUMBER,
  STRING,
  PUNCT,
  SEMICOLON,
  LPAREN,
  RPAREN,
  COMMA,
  STAR,
  EOF_TOKEN,
  INVALID
};

struct Token {
  TokenType type;
  string text;
  int32_t line;
  int32_t col;
};

const char* TokenTypeToString(TokenType t);
bool IsKeyword(const string& s, const char* kw);

} // namespace minidb