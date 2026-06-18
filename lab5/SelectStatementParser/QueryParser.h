#ifndef QUERY_PARSER_H
#define QUERY_PARSER_H

#include "Expression.h"
#include "token.h"
#include <cstddef>
#include <string>
#include <vector>

class QueryParser {

public:
  QueryParser() = default;
  ~QueryParser() = default;

  void tokenize(const std::string &query);

  typedef SelectStatement ParseResult;

  // TODO: This should be a variant of different statements
  ParseResult parse();

private:
  size_t m_TokenPos;
  std::vector<Token> m_Tokens;

  const Token &consume(TokenType type);

  SelectStatement parseSelectStatement();

  // Specific parsers
  std::vector<std::string> parseProjection();
  std::string parseTableName();
  Expression *parseFilterExpr();
};

#endif
