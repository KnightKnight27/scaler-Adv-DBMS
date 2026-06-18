#include "QueryParser.h"
#include "Expression.h"
#include "token.h"
#include <cassert>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

void QueryParser::tokenize(const std::string &query) {
  using std::isspace, std::isalpha, std::isdigit;

  size_t i = 0;

  auto getWord = [&query, &i]() -> std::string {
    std::string word;
    while (i < query.size()) {
      if (isspace(query[i]) || query[i] == '<' || query[i] == '>' ||
          query[i] == '=' || query[i] == '(' || query[i] == ')' ||
          query[i] == ';' || query[i] == ',') {
        break;
      }

      word += query[i];
      ++i;
    }
    return word;
  };

  auto isNumber = [](const std::string &word) -> char * {
    char *num;
    strtol(word.c_str(), &num, 10);

    return num;
  };

  while (i < query.size()) {
    auto c = static_cast<unsigned char>(query[i]);

    if (isalpha(c) || c == '_') {

      // Word starts with an alphabet or _
      // Likely a SQL keyword | string literal | IDENTIFIER

      std::string word = getWord();

      if (word == "SELECT") {
        m_Tokens.emplace_back("SELECT", TokenType::SELECT);
      } else if (word == "FROM") {
        m_Tokens.emplace_back("FROM", TokenType::FROM);
      } else if (word == "WHERE") {
        m_Tokens.emplace_back("WHERE", TokenType::WHERE);
      } else if (word == "OR") {
        m_Tokens.emplace_back("OR", TokenType::OR);
      } else if (word == "AND") {
        m_Tokens.emplace_back("AND", TokenType::AND);
      } else {
        m_Tokens.emplace_back(word, TokenType::IDENTIFIER);
      }
    } else if (c == '\"') {
      std::string word = getWord();

      if (word.size() == 1 || word.back() != '\"') {
        throw std::runtime_error(
            "[tokenizer] Missing end quotation near position " +
            std::to_string(i));
      }

      std::string literal = word.substr(1, word.size() - 2);
      m_Tokens.emplace_back(literal, TokenType::STRING_LITERAL);
    } else if (isdigit(c)) {
      std::string word = getWord();
      m_Tokens.emplace_back(word, TokenType::NUMERIC_LITERAL);
    } else if (isspace(c)) {
      ++i;
    } else {
      if (c == '<') {
        m_Tokens.emplace_back("<", TokenType::LESS_THAN);
        ++i;
      } else if (c == '>') {
        m_Tokens.emplace_back(">", TokenType::GREATER_THAN);
        ++i;
      } else if (c == '=') {
        m_Tokens.emplace_back("=", TokenType::EQUALS);
        ++i;
      } else if (c == '(') {
        m_Tokens.emplace_back("(", TokenType::LPAREN);
        ++i;
      } else if (c == ')') {
        m_Tokens.emplace_back(")", TokenType::RPAREN);
        ++i;
      } else if (c == ',') {
        m_Tokens.emplace_back(",", TokenType::COMMA);
      } else if (c == ';') {
        m_Tokens.emplace_back(";", TokenType::END);
        break;
      } else {
        std::string error = "[tokenizer] Unrecognized symbol (" +
                            std::string(1, c) + ") CODE " +
                            std::to_string((int)c) + " near position " +
                            std::to_string(i);
        throw std::runtime_error(error);
      }
    }
  }

  if (m_Tokens.back().type != TokenType::END) {
    throw std::runtime_error("[tokenizer] No semicolon at end of query");
  }
}

const Token &QueryParser::consume(TokenType type) {
  assert(m_TokenPos < m_Tokens.size());

  const Token &token = m_Tokens[m_TokenPos];

  if (token.type != type) {
    throw std::runtime_error(
        "[Parser] Expected and actual token types do not match.\n \
            Expected: " +
        token.getTypeName(type) + '\n' + "Got: " + token.getTypeName());
  }

  ++m_TokenPos;

  return token;
}

SelectStatement QueryParser::parseSelectStatement() {
  consume(TokenType::SELECT);

  std::vector<std::string> projection = parseProjection();

  consume(TokenType::FROM);

  std::string tableName = parseTableName();

  consume(TokenType::WHERE);

  Expression *filterExpression = parseFilterExpr();

  return {.projection = projection,
          .tableName = tableName,
          .filter = filterExpression};
}

std::vector<std::string> QueryParser::parseProjection() {
  std::vector<std::string> result;

  while (m_Tokens[m_TokenPos].type == TokenType::IDENTIFIER &&
         m_TokenPos + 1 < m_Tokens.size() &&
         m_Tokens[m_TokenPos + 1].type == TokenType::COMMA) {
    result.emplace_back(consume(TokenType::IDENTIFIER).value);

    consume(TokenType::COMMA);
  }

  result.emplace_back(consume(TokenType::IDENTIFIER).value);

  return result;
}

std::string QueryParser::parseTableName() {
  return consume(TokenType::IDENTIFIER).value;
}

Expression *parseFilterExpr() {
  // TODO: TO BE IMPLEMENTED
  return nullptr;
}

QueryParser::ParseResult QueryParser::parse() {
  if (m_Tokens.empty()) {
    throw new std::runtime_error("[Parser] Nothing to parse");
  }

  Token &firstToken = m_Tokens[0];

  if (m_Tokens[0].type == TokenType::SELECT) {
    return parseSelectStatement();
  }

  throw std::runtime_error("[Parser] Invalid or unsupported query type - " +
                           firstToken.value);
}
