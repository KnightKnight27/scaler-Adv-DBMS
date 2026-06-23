#include "parser/tokenizer.h"

#include <cctype>

namespace minidb {

using namespace std;

Tokenizer::Tokenizer(const string& src) : src_(src) {}

void Tokenizer::SkipWhitespace() {
  while (pos_ < src_.size()) {
    char c = src_[pos_];
    if (c == ' ' || c == '\t' || c == '\r') {
      ++pos_;
      ++col_;
    } else if (c == '\n') {
      ++pos_;
      ++line_;
      col_ = 1;
    } else if (c == '-' && pos_ + 1 < src_.size() && src_[pos_ + 1] == '-') {
      while (pos_ < src_.size() && src_[pos_] != '\n')
        ++pos_;
    } else {
      break;
    }
  }
}

bool Tokenizer::IsIdentStart(char c) const {
  return isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool Tokenizer::IsIdentBody(char c) const {
  return isalnum(static_cast<unsigned char>(c)) || c == '_';
}

Token Tokenizer::ReadNumber() {
  int32_t startLine = line_;
  int32_t startCol = col_;
  size_t start = pos_;
  while (pos_ < src_.size() && isdigit(static_cast<unsigned char>(src_[pos_]))) {
    ++pos_;
    ++col_;
  }
  return {TokenType::NUMBER, src_.substr(start, pos_ - start), startLine, startCol};
}

Token Tokenizer::ReadString() {
  int32_t startLine = line_;
  int32_t startCol = col_;
  ++pos_;
  ++col_;
  size_t start = pos_;
  while (pos_ < src_.size() && src_[pos_] != '\'') {
    if (src_[pos_] == '\n') {
      ++line_;
      col_ = 1;
    }
    ++pos_;
    ++col_;
  }
  string s = src_.substr(start, pos_ - start);
  if (pos_ < src_.size()) {
    ++pos_;
    ++col_;
  }
  return {TokenType::STRING, s, startLine, startCol};
}

Token Tokenizer::ReadIdentOrKeyword() {
  int32_t startLine = line_;
  int32_t startCol = col_;
  size_t start = pos_;
  while (pos_ < src_.size() && IsIdentBody(src_[pos_])) {
    ++pos_;
    ++col_;
  }
  string text = src_.substr(start, pos_ - start);
  TokenType type = TokenType::IDENT;
  if (IsKeyword(text, "SELECT") || IsKeyword(text, "FROM") || IsKeyword(text, "WHERE") ||
      IsKeyword(text, "INSERT") || IsKeyword(text, "INTO") || IsKeyword(text, "VALUES") ||
      IsKeyword(text, "CREATE") || IsKeyword(text, "TABLE") || IsKeyword(text, "DROP") ||
      IsKeyword(text, "DELETE") || IsKeyword(text, "UPDATE") || IsKeyword(text, "SET") ||
      IsKeyword(text, "INT") || IsKeyword(text, "INTEGER") || IsKeyword(text, "VARCHAR") ||
      IsKeyword(text, "BOOLEAN") || IsKeyword(text, "BOOL") || IsKeyword(text, "BIGINT") ||
      IsKeyword(text, "PRIMARY") || IsKeyword(text, "KEY") || IsKeyword(text, "NOT") ||
      IsKeyword(text, "NULL") || IsKeyword(text, "AS") || IsKeyword(text, "AND") ||
      IsKeyword(text, "OR") || IsKeyword(text, "TRUE") || IsKeyword(text, "FALSE") ||
      IsKeyword(text, "GROUP") || IsKeyword(text, "BY") || IsKeyword(text, "ORDER") ||
      IsKeyword(text, "ASC") || IsKeyword(text, "DESC") || IsKeyword(text, "HAVING") ||
      IsKeyword(text, "LIMIT")) {
    type = TokenType::KEYWORD;
  }
  return {type, text, startLine, startCol};
}

vector<Token> Tokenizer::TokenizeAll() {
  vector<Token> tokens;
  while (pos_ < src_.size()) {
    SkipWhitespace();
    if (pos_ >= src_.size())
      break;
    char c = src_[pos_];
    if (isdigit(static_cast<unsigned char>(c))) {
      tokens.push_back(ReadNumber());
    } else if (c == '\'') {
      tokens.push_back(ReadString());
    } else if (IsIdentStart(c)) {
      tokens.push_back(ReadIdentOrKeyword());
    } else if (c == ';') {
      tokens.push_back({TokenType::SEMICOLON, ";", line_, col_});
      ++pos_;
      ++col_;
    } else if (c == '(') {
      tokens.push_back({TokenType::LPAREN, "(", line_, col_});
      ++pos_;
      ++col_;
    } else if (c == ')') {
      tokens.push_back({TokenType::RPAREN, ")", line_, col_});
      ++pos_;
      ++col_;
    } else if (c == ',') {
      tokens.push_back({TokenType::COMMA, ",", line_, col_});
      ++pos_;
      ++col_;
    } else if (c == '*') {
      tokens.push_back({TokenType::STAR, "*", line_, col_});
      ++pos_;
      ++col_;
    } else {
      tokens.push_back({TokenType::PUNCT, string(1, c), line_, col_});
      ++pos_;
      ++col_;
    }
  }
  tokens.push_back({TokenType::EOF_TOKEN, "", line_, col_});
  return tokens;
}

} // namespace minidb
