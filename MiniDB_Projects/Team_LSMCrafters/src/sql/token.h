#pragma once
#include <string>

namespace minidb {

enum class TokenType {
  // keywords
  Select, From, Where, Insert, Into, Values, Delete, Join, On, And, Or, Explain,
  // literals / identifiers
  Identifier, Number, String,
  // operators and punctuation
  Eq, Ne, Lt, Gt, Le, Ge, Comma, LParen, RParen, Star,
  End
};

struct Token {
  TokenType   type;
  std::string text;  // raw lexeme (identifier name, number text, string value)
};

}  // namespace minidb
