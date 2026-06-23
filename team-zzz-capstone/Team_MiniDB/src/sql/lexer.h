#pragma once
#include <string>
#include <vector>

namespace minidb {

enum class TokenType {
  IDENT, NUMBER, STRING,
  KW_SELECT, KW_FROM, KW_WHERE, KW_JOIN, KW_ON, KW_INSERT, KW_INTO, KW_VALUES,
  KW_DELETE, KW_CREATE, KW_TABLE, KW_PRIMARY, KW_KEY, KW_INT, KW_VARCHAR, KW_AND, KW_AS,
  STAR, COMMA, LPAREN, RPAREN, DOT, SEMI,
  EQ, NEQ, LT, LTE, GT, GTE,
  END
};

struct Token {
  TokenType type;
  std::string text;
};

// Simple hand-written tokenizer; case-insensitive keywords, single-quoted
// strings, bare integers, and the small set of punctuation MiniDB's
// grammar needs.
class Lexer {
 public:
  explicit Lexer(std::string src) : src_(std::move(src)) {}
  std::vector<Token> Tokenize();

 private:
  std::string src_;
  size_t pos_ = 0;
};

}  // namespace minidb
