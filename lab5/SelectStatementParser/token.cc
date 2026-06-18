#include "token.h"

Token::Token(const std::string &valueA, TokenType typeA)
    : value(valueA), type(typeA) {}

std::string Token::getTypeName() const {
  switch (type) {
  case TokenType::SELECT:
    return "SELECT";

  case TokenType::FROM:
    return "FROM";

  case TokenType::WHERE:
    return "WHERE";

  case TokenType::OR:
    return "OR";

  case TokenType::AND:
    return "AND";

  case TokenType::IDENTIFIER:
    return "IDENTIFIER";

  case TokenType::STRING_LITERAL:
    return "STRING_LITERAL";

  case TokenType::NUMERIC_LITERAL:
    return "NUMERIC_LITERAL";

  case TokenType::EQUALS:
    return "EQUALS";

  case TokenType::GREATER_THAN:
    return "GREATER_THAN";

  case TokenType::LESS_THAN:
    return "LESS_THAN";

  case TokenType::LPAREN:
    return "LPAREN";

  case TokenType::RPAREN:
    return "RPAREN";

  case TokenType::END:
    return "END";

  default:
    return "UNKNOWN";
  }
}

std::string Token::getTypeName(TokenType typeA) const {
  switch (typeA) {
  case TokenType::SELECT:
    return "SELECT";

  case TokenType::FROM:
    return "FROM";

  case TokenType::WHERE:
    return "WHERE";

  case TokenType::OR:
    return "OR";

  case TokenType::AND:
    return "AND";

  case TokenType::IDENTIFIER:
    return "IDENTIFIER";

  case TokenType::STRING_LITERAL:
    return "STRING_LITERAL";

  case TokenType::NUMERIC_LITERAL:
    return "NUMERIC_LITERAL";

  case TokenType::EQUALS:
    return "EQUALS";

  case TokenType::GREATER_THAN:
    return "GREATER_THAN";

  case TokenType::LESS_THAN:
    return "LESS_THAN";

  case TokenType::LPAREN:
    return "LPAREN";

  case TokenType::RPAREN:
    return "RPAREN";

  case TokenType::END:
    return "END";

  default:
    return "UNKNOWN";
  }
}
