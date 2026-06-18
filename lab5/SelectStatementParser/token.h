#ifndef TOKEN_H
#define TOKEN_H

#include <string>

enum class TokenType {
  SELECT = 0,
  FROM,
  WHERE,
  OR,
  AND,

  IDENTIFIER,
  STRING_LITERAL,
  NUMERIC_LITERAL,

  EQUALS,
  GREATER_THAN,
  LESS_THAN,
  LPAREN,
  RPAREN,
  COMMA,
  END
};

class Token {

public:
  Token(const std::string &valueA, TokenType typeA);

  std::string value;
  TokenType type;

  std::string getTypeName() const;
  std::string getTypeName(TokenType typeA) const;
};

#endif
