// ============================================================================
// lexer.h  --  Turns raw SQL text into a stream of TOKENS.
//
// Tokenizing (a.k.a. lexing) is the first step of parsing.  It groups
// characters into meaningful chunks so the parser deals with "the keyword
// SELECT" or "the number 30" instead of individual letters.  Example:
//
//   "SELECT age FROM users"
//        -> [SELECT] [age] [FROM] [users]
//
// We keep four token kinds: identifiers/keywords, integer literals, string
// literals ('...'), and symbols (punctuation/operators like ( ) , = >= ).
// ============================================================================
#pragma once

#include "common/common.h"

namespace minidb {

enum class TokKind { kIdent, kNumber, kString, kSymbol, kEnd };

struct Token {
  TokKind     kind;
  string text;   // the raw text, e.g. "SELECT", "30", ">=", "users"
};

class Lexer {
 public:
  explicit Lexer(const string &sql) : s_(sql) {}

  // Produce all tokens up to end-of-input (which is a kEnd token).
  vector<Token> tokenize() {
    vector<Token> out;
    while (true) {
      skipSpace();
      if (pos_ >= s_.size()) { out.push_back({TokKind::kEnd, ""}); break; }
      char c = s_[pos_];
      if (isalpha((unsigned char)c) || c == '_') {
        out.push_back(readIdent());
      } else if (isdigit((unsigned char)c) ||
                 (c == '-' && pos_ + 1 < s_.size() &&
                  isdigit((unsigned char)s_[pos_ + 1]))) {
        out.push_back(readNumber());
      } else if (c == '\'') {
        out.push_back(readString());
      } else {
        out.push_back(readSymbol());
      }
    }
    return out;
  }

 private:
  void skipSpace() {
    while (pos_ < s_.size() && isspace((unsigned char)s_[pos_])) ++pos_;
  }

  Token readIdent() {
    size_t start = pos_;
    while (pos_ < s_.size() &&
           (isalnum((unsigned char)s_[pos_]) || s_[pos_] == '_')) ++pos_;
    return {TokKind::kIdent, s_.substr(start, pos_ - start)};
  }

  Token readNumber() {
    size_t start = pos_;
    if (s_[pos_] == '-') ++pos_;            // optional leading minus
    while (pos_ < s_.size() && isdigit((unsigned char)s_[pos_])) ++pos_;
    return {TokKind::kNumber, s_.substr(start, pos_ - start)};
  }

  Token readString() {
    ++pos_;                                  // skip opening quote
    string val;
    while (pos_ < s_.size() && s_[pos_] != '\'') val += s_[pos_++];
    if (pos_ < s_.size()) ++pos_;            // skip closing quote
    return {TokKind::kString, val};
  }

  // Multi-character symbols (>=, <=, !=) are recognized first, else one char.
  Token readSymbol() {
    if (pos_ + 1 < s_.size()) {
      string two = s_.substr(pos_, 2);
      if (two == ">=" || two == "<=" || two == "!=" || two == "<>") {
        pos_ += 2;
        return {TokKind::kSymbol, two};
      }
    }
    return {TokKind::kSymbol, string(1, s_[pos_++])};
  }

  string s_;
  size_t      pos_{0};
};

}  // namespace minidb
