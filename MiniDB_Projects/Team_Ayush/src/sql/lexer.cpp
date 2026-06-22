#include "sql/lexer.h"

#include <cctype>

namespace minidb {

std::vector<Token> Tokenize(const std::string& sql) {
  std::vector<Token> out;
  size_t i = 0, n = sql.size();
  while (i < n) {
    char c = sql[i];
    if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }

    // String literal 'like this'
    if (c == '\'') {
      ++i;
      std::string s;
      while (i < n && sql[i] != '\'') s += sql[i++];
      if (i < n) ++i;  // closing quote
      out.push_back({TokType::STRING, s});
      continue;
    }

    // Number (optionally negative handled by parser via unary; here digits only)
    if (std::isdigit(static_cast<unsigned char>(c))) {
      std::string s;
      while (i < n && std::isdigit(static_cast<unsigned char>(sql[i]))) s += sql[i++];
      out.push_back({TokType::NUMBER, s});
      continue;
    }

    // Identifier / keyword
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
      std::string s;
      while (i < n && (std::isalnum(static_cast<unsigned char>(sql[i])) || sql[i] == '_'))
        s += sql[i++];
      out.push_back({TokType::IDENT, s});
      continue;
    }

    // Multi-char operators
    if (i + 1 < n) {
      std::string two = sql.substr(i, 2);
      if (two == "<=" || two == ">=" || two == "!=" || two == "<>") {
        out.push_back({TokType::PUNCT, two});
        i += 2;
        continue;
      }
    }

    // Single-char punctuation
    out.push_back({TokType::PUNCT, std::string(1, c)});
    ++i;
  }
  out.push_back({TokType::END, ""});
  return out;
}

}  // namespace minidb
