#include "minidb/sql/lexer.hpp"

#include <cctype>
#include <stdexcept>
#include <unordered_set>

namespace minidb {

namespace {
const std::unordered_set<std::string>& keywords() {
  static const std::unordered_set<std::string> kw = {
      "create", "table", "insert", "into", "values", "select", "from",
      "where", "delete", "and", "int", "integer", "varchar", "text",
      "double", "float", "primary", "key", "begin", "commit", "rollback",
      "order", "by", "index", "on", "inner", "join", "analyze", "explain"};
  return kw;
}
std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
}  // namespace

std::vector<Token> Lexer::tokenize() {
  std::vector<Token> out;
  while (pos_ < src_.size()) {
    char c = src_[pos_];
    if (std::isspace(static_cast<unsigned char>(c))) { ++pos_; continue; }

    // Punctuation and operators.
    switch (c) {
      case ',': out.push_back({TokKind::Comma, ","}); ++pos_; continue;
      case '(': out.push_back({TokKind::LParen, "("}); ++pos_; continue;
      case ')': out.push_back({TokKind::RParen, ")"}); ++pos_; continue;
      case '*': out.push_back({TokKind::Star, "*"}); ++pos_; continue;
      case ';': out.push_back({TokKind::Semicolon, ";"}); ++pos_; continue;
      case '.': out.push_back({TokKind::Dot, "."}); ++pos_; continue;
      case '=': out.push_back({TokKind::Eq, "="}); ++pos_; continue;
      case '!':
        if (pos_ + 1 < src_.size() && src_[pos_ + 1] == '=') {
          out.push_back({TokKind::Ne, "!="}); pos_ += 2; continue;
        }
        throw std::runtime_error("lexer: unexpected '!'");
      case '<':
        if (pos_ + 1 < src_.size() && src_[pos_ + 1] == '=') {
          out.push_back({TokKind::Le, "<="}); pos_ += 2; continue;
        }
        out.push_back({TokKind::Lt, "<"}); ++pos_; continue;
      case '>':
        if (pos_ + 1 < src_.size() && src_[pos_ + 1] == '=') {
          out.push_back({TokKind::Ge, ">="}); pos_ += 2; continue;
        }
        out.push_back({TokKind::Gt, ">"}); ++pos_; continue;
    }

    // String literal: '...'
    if (c == '\'') {
      ++pos_;
      std::string s;
      while (pos_ < src_.size() && src_[pos_] != '\'') {
        if (src_[pos_] == '\\' && pos_ + 1 < src_.size()) { ++pos_; }
        s.push_back(src_[pos_]);
        ++pos_;
      }
      if (pos_ >= src_.size()) throw std::runtime_error("lexer: unterminated string");
      ++pos_;  // closing quote
      out.push_back({TokKind::StringLit, s});
      continue;
    }

    // Number: integer or double (optional leading '-').
    if (std::isdigit(static_cast<unsigned char>(c)) ||
        (c == '-' && pos_ + 1 < src_.size() &&
         std::isdigit(static_cast<unsigned char>(src_[pos_ + 1])))) {
      size_t start = pos_;
      if (c == '-') ++pos_;
      bool is_double = false;
      while (pos_ < src_.size() &&
             (std::isdigit(static_cast<unsigned char>(src_[pos_])) || src_[pos_] == '.')) {
        if (src_[pos_] == '.') is_double = true;
        ++pos_;
      }
      std::string num = src_.substr(start, pos_ - start);
      Token t;
      if (is_double) { t.kind = TokKind::DoubleLit; t.dbl_val = std::stod(num); }
      else { t.kind = TokKind::IntLit; t.int_val = std::stoll(num); }
      t.text = num;
      out.push_back(t);
      continue;
    }

    // Identifier or keyword.
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
      size_t start = pos_;
      while (pos_ < src_.size() &&
             (std::isalnum(static_cast<unsigned char>(src_[pos_])) || src_[pos_] == '_'))
        ++pos_;
      std::string word = src_.substr(start, pos_ - start);
      std::string lw = lower(word);
      if (keywords().count(lw)) out.push_back({TokKind::Keyword, lw});
      else out.push_back({TokKind::Ident, word});
      continue;
    }

    throw std::runtime_error(std::string("lexer: unexpected character '") + c + "'");
  }
  out.push_back({TokKind::End, ""});
  return out;
}

}  // namespace minidb
