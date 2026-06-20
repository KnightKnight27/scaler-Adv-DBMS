#include "lexer.h"

#include <cctype>
#include <stdexcept>
#include <unordered_map>

namespace minidb {

namespace {
const std::unordered_map<std::string, TokenType> kKeywords = {
    {"SELECT", TokenType::KW_SELECT}, {"FROM", TokenType::KW_FROM},
    {"WHERE", TokenType::KW_WHERE},   {"JOIN", TokenType::KW_JOIN},
    {"ON", TokenType::KW_ON},         {"INSERT", TokenType::KW_INSERT},
    {"INTO", TokenType::KW_INTO},     {"VALUES", TokenType::KW_VALUES},
    {"DELETE", TokenType::KW_DELETE}, {"CREATE", TokenType::KW_CREATE},
    {"TABLE", TokenType::KW_TABLE},   {"PRIMARY", TokenType::KW_PRIMARY},
    {"KEY", TokenType::KW_KEY},       {"INT", TokenType::KW_INT},
    {"INTEGER", TokenType::KW_INT},   {"VARCHAR", TokenType::KW_VARCHAR},
    {"TEXT", TokenType::KW_VARCHAR},  {"AND", TokenType::KW_AND},
    {"AS", TokenType::KW_AS},
};

std::string ToUpper(std::string s) {
  for (auto &c : s) c = static_cast<char>(std::toupper(c));
  return s;
}
}  // namespace

std::vector<Token> Lexer::Tokenize() {
  std::vector<Token> tokens;
  while (pos_ < src_.size()) {
    char c = src_[pos_];
    if (std::isspace(static_cast<unsigned char>(c))) {
      pos_++;
      continue;
    }
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
      size_t start = pos_;
      while (pos_ < src_.size() && (std::isalnum(static_cast<unsigned char>(src_[pos_])) || src_[pos_] == '_')) pos_++;
      std::string word = src_.substr(start, pos_ - start);
      auto it = kKeywords.find(ToUpper(word));
      if (it != kKeywords.end()) tokens.push_back({it->second, word});
      else tokens.push_back({TokenType::IDENT, word});
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(c)) || (c == '-' && pos_ + 1 < src_.size() && std::isdigit((unsigned char)src_[pos_ + 1]))) {
      size_t start = pos_++;
      while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_]))) pos_++;
      tokens.push_back({TokenType::NUMBER, src_.substr(start, pos_ - start)});
      continue;
    }
    if (c == '\'') {
      pos_++;
      // Standard SQL escaping: a doubled quote ('') inside a string
      // literal is a literal single quote, not the end of the string.
      std::string s;
      while (pos_ < src_.size()) {
        if (src_[pos_] == '\'') {
          if (pos_ + 1 < src_.size() && src_[pos_ + 1] == '\'') {
            s += '\'';
            pos_ += 2;
            continue;
          }
          break;
        }
        s += src_[pos_++];
      }
      if (pos_ < src_.size()) pos_++;  // closing quote
      tokens.push_back({TokenType::STRING, s});
      continue;
    }
    switch (c) {
      case '*': tokens.push_back({TokenType::STAR, "*"}); pos_++; continue;
      case ',': tokens.push_back({TokenType::COMMA, ","}); pos_++; continue;
      case '(': tokens.push_back({TokenType::LPAREN, "("}); pos_++; continue;
      case ')': tokens.push_back({TokenType::RPAREN, ")"}); pos_++; continue;
      case '.': tokens.push_back({TokenType::DOT, "."}); pos_++; continue;
      case ';': tokens.push_back({TokenType::SEMI, ";"}); pos_++; continue;
      case '=': tokens.push_back({TokenType::EQ, "="}); pos_++; continue;
      case '<':
        pos_++;
        if (pos_ < src_.size() && src_[pos_] == '=') { tokens.push_back({TokenType::LTE, "<="}); pos_++; }
        else tokens.push_back({TokenType::LT, "<"});
        continue;
      case '>':
        pos_++;
        if (pos_ < src_.size() && src_[pos_] == '=') { tokens.push_back({TokenType::GTE, ">="}); pos_++; }
        else tokens.push_back({TokenType::GT, ">"});
        continue;
      case '!':
        pos_++;
        if (pos_ < src_.size() && src_[pos_] == '=') { tokens.push_back({TokenType::NEQ, "!="}); pos_++; }
        else throw std::runtime_error("unexpected '!'");
        continue;
      default:
        throw std::runtime_error(std::string("unexpected character: ") + c);
    }
  }
  tokens.push_back({TokenType::END, ""});
  return tokens;
}

}  // namespace minidb
