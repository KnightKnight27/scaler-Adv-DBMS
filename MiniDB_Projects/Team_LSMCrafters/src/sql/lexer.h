#pragma once
#include <vector>
#include "sql/token.h"

namespace minidb {

// Turns a SQL string into a list of tokens (ending with an End token).
// Keywords are case-insensitive; identifiers and numbers are not.
class Lexer {
 public:
  explicit Lexer(std::string sql) : sql_(std::move(sql)) {}
  std::vector<Token> tokenize();

 private:
  std::string sql_;
};

}  // namespace minidb
