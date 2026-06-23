#include "sql/lexer.h"
#include <cctype>
#include <stdexcept>
#include <unordered_map>

namespace minidb {

namespace {

const std::unordered_map<std::string, TokenType>& keywords() {
  static const std::unordered_map<std::string, TokenType> kw = {
      {"select", TokenType::Select}, {"from", TokenType::From},
      {"where", TokenType::Where},   {"insert", TokenType::Insert},
      {"into", TokenType::Into},     {"values", TokenType::Values},
      {"delete", TokenType::Delete}, {"join", TokenType::Join},
      {"on", TokenType::On},         {"and", TokenType::And},
      {"or", TokenType::Or},         {"explain", TokenType::Explain},
  };
  return kw;
}

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

}  // namespace

std::vector<Token> Lexer::tokenize() {
  std::vector<Token> out;
  std::size_t i = 0;
  const std::size_t n = sql_.size();

  while (i < n) {
    char c = sql_[i];
    if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }

    // identifiers / keywords ('.' is allowed so "table.column" is one token)
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
      std::size_t start = i;
      while (i < n && (std::isalnum(static_cast<unsigned char>(sql_[i])) ||
                       sql_[i] == '_' || sql_[i] == '.'))
        ++i;
      std::string word = sql_.substr(start, i - start);
      auto it = keywords().find(lower(word));
      if (it != keywords().end()) out.push_back({it->second, word});
      else out.push_back({TokenType::Identifier, word});
      continue;
    }

    // numbers (optionally negative)
    if (std::isdigit(static_cast<unsigned char>(c)) ||
        (c == '-' && i + 1 < n && std::isdigit(static_cast<unsigned char>(sql_[i + 1])))) {
      std::size_t start = i++;
      while (i < n && std::isdigit(static_cast<unsigned char>(sql_[i]))) ++i;
      out.push_back({TokenType::Number, sql_.substr(start, i - start)});
      continue;
    }

    // string literals 'like this'
    if (c == '\'') {
      std::size_t start = ++i;
      while (i < n && sql_[i] != '\'') ++i;
      if (i >= n) throw std::runtime_error("lexer: unterminated string literal");
      out.push_back({TokenType::String, sql_.substr(start, i - start)});
      ++i;  // closing quote
      continue;
    }

    // operators and punctuation
    switch (c) {
      case ',': out.push_back({TokenType::Comma, ","}); ++i; break;
      case '(': out.push_back({TokenType::LParen, "("}); ++i; break;
      case ')': out.push_back({TokenType::RParen, ")"}); ++i; break;
      case '*': out.push_back({TokenType::Star, "*"}); ++i; break;
      case '=': out.push_back({TokenType::Eq, "="}); ++i; break;
      case '<':
        if (i + 1 < n && sql_[i + 1] == '=') { out.push_back({TokenType::Le, "<="}); i += 2; }
        else { out.push_back({TokenType::Lt, "<"}); ++i; }
        break;
      case '>':
        if (i + 1 < n && sql_[i + 1] == '=') { out.push_back({TokenType::Ge, ">="}); i += 2; }
        else { out.push_back({TokenType::Gt, ">"}); ++i; }
        break;
      case '!':
        if (i + 1 < n && sql_[i + 1] == '=') { out.push_back({TokenType::Ne, "!="}); i += 2; }
        else throw std::runtime_error("lexer: expected '=' after '!'");
        break;
      default:
        throw std::runtime_error(std::string("lexer: unexpected character '") + c + "'");
    }
  }

  out.push_back({TokenType::End, ""});
  return out;
}

}  // namespace minidb
