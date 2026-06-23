#pragma once
// Hand-written tokenizer for MiniDB SQL.
#include <string>
#include <vector>

namespace minidb {

enum class TokKind {
  Ident, Keyword, IntLit, DoubleLit, StringLit,
  Comma, LParen, RParen, Star, Semicolon, Dot,
  Eq, Ne, Lt, Le, Gt, Ge,
  End
};

struct Token {
  TokKind kind;
  std::string text;   // raw lexeme (lowercased for keywords)
  long long int_val = 0;
  double dbl_val = 0;
};

class Lexer {
 public:
  explicit Lexer(std::string src) : src_(std::move(src)) {}
  std::vector<Token> tokenize();  // throws std::runtime_error on bad input

 private:
  std::string src_;
  size_t pos_ = 0;
};

}  // namespace minidb
