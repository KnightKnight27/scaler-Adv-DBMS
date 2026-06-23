#pragma once

#include "parser/token.h"

#include <vector>

namespace minidb {

using namespace std;

class Tokenizer {
public:
  explicit Tokenizer(const string& src);
  vector<Token> TokenizeAll();

private:
  void SkipWhitespace();
  bool IsIdentStart(char c) const;
  bool IsIdentBody(char c) const;
  Token ReadNumber();
  Token ReadString();
  Token ReadIdentOrKeyword();

  string src_;
  size_t pos_ = 0;
  int32_t line_ = 1;
  int32_t col_ = 1;
};

} // namespace minidb