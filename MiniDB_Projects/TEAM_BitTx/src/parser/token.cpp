#include "parser/token.h"

#include <cctype>
#include <cstring>

namespace minidb {

using namespace std;

const char* TokenTypeToString(TokenType t) {
  switch (t) {
  case TokenType::KEYWORD:
    return "KEYWORD";
  case TokenType::IDENT:
    return "IDENT";
  case TokenType::NUMBER:
    return "NUMBER";
  case TokenType::STRING:
    return "STRING";
  case TokenType::PUNCT:
    return "PUNCT";
  case TokenType::SEMICOLON:
    return "SEMICOLON";
  case TokenType::LPAREN:
    return "LPAREN";
  case TokenType::RPAREN:
    return "RPAREN";
  case TokenType::COMMA:
    return "COMMA";
  case TokenType::STAR:
    return "STAR";
  case TokenType::EOF_TOKEN:
    return "EOF";
  default:
    return "INVALID";
  }
}

bool IsKeyword(const string& s, const char* kw) {
  return strcasecmp(s.c_str(), kw) == 0;
}

} // namespace minidb