#include <cctype>
#include <unordered_set>

#include "common/exception.h"
#include "sql/sql.h"

namespace minidb {

static const std::unordered_set<std::string> kKeywords = {
    "CREATE", "TABLE", "INDEX",   "ON",     "INSERT", "INTO",  "VALUES",
    "SELECT", "FROM",  "WHERE",   "DELETE", "JOIN",   "INNER", "AND",
    "OR",     "INT",   "INTEGER", "VARCHAR", "TEXT"};

std::vector<Token> Lexer::Tokenize() {
  std::vector<Token> out;
  size_t i = 0;
  size_t n = sql_.size();
  while (i < n) {
    char c = sql_[i];
    if (std::isspace(static_cast<unsigned char>(c))) {
      i++;
      continue;
    }
    // string literal in single quotes
    if (c == '\'') {
      i++;
      std::string s;
      while (i < n && sql_[i] != '\'') s.push_back(sql_[i++]);
      if (i >= n) throw Exception(ErrorKind::kParse, "unterminated string literal");
      i++;  // closing quote
      out.push_back({TokenType::kString, s});
      continue;
    }
    // number (optionally negative handled at parse level via unary; here digits)
    if (std::isdigit(static_cast<unsigned char>(c))) {
      std::string num;
      while (i < n && std::isdigit(static_cast<unsigned char>(sql_[i]))) num.push_back(sql_[i++]);
      out.push_back({TokenType::kNumber, num});
      continue;
    }
    // identifier or keyword
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
      std::string id;
      while (i < n && (std::isalnum(static_cast<unsigned char>(sql_[i])) || sql_[i] == '_')) {
        id.push_back(sql_[i++]);
      }
      std::string upper;
      for (char ch : id) upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
      if (kKeywords.count(upper) != 0) {
        out.push_back({TokenType::kKeyword, upper});
      } else {
        out.push_back({TokenType::kIdentifier, id});
      }
      continue;
    }
    // multi-char operators: <=, >=, !=, <>
    if (i + 1 < n) {
      std::string two = sql_.substr(i, 2);
      if (two == "<=" || two == ">=" || two == "!=" || two == "<>") {
        out.push_back({TokenType::kPunct, two});
        i += 2;
        continue;
      }
    }
    // single-char punctuation / operators
    if (std::string("(),*.=<>;").find(c) != std::string::npos) {
      out.push_back({TokenType::kPunct, std::string(1, c)});
      i++;
      continue;
    }
    throw Exception(ErrorKind::kParse, std::string("unexpected character '") + c + "'");
  }
  out.push_back({TokenType::kEnd, ""});
  return out;
}

}  // namespace minidb
