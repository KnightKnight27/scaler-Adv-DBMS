#pragma once
#include <vector>

#include "ast.h"
#include "lexer.h"

namespace minidb {

// Recursive-descent parser for the subset of SQL MiniDB supports:
//   CREATE TABLE t (col TYPE [PRIMARY KEY], ...)
//   INSERT INTO t VALUES (v, ...)
//   SELECT (* | col[, col...]) FROM t [JOIN t2 ON c1 = c2] [WHERE cond [AND cond...]]
//   DELETE FROM t [WHERE cond [AND cond...]]
class Parser {
 public:
  explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}
  Statement ParseStatement();

 private:
  const Token &Peek() const { return tokens_[pos_]; }
  const Token &Advance() { return tokens_[pos_++]; }
  bool Check(TokenType t) const { return Peek().type == t; }
  const Token &Expect(TokenType t, const char *what);

  CreateTableStmt ParseCreateTable();
  InsertStmt ParseInsert();
  SelectStmt ParseSelect();
  DeleteStmt ParseDelete();
  Comparison ParseComparison();
  Operand ParseOperand();
  ColumnRef ParseColumnRef();
  Value ParseLiteral();

  std::vector<Token> tokens_;
  size_t pos_ = 0;
};

}  // namespace minidb
