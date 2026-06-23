#pragma once

#include "parser/ast.h"
#include "parser/token.h"

#include <memory>
#include <vector>

namespace minidb {

using namespace std;

class Parser {
public:
  explicit Parser(vector<Token> tokens);

  unique_ptr<Stmt> ParseStatement();

private:
  const Token& Peek() const;
  const Token& Advance();
  bool Match(TokenType t);
  bool Check(TokenType t) const;
  bool Expect(TokenType t, const char* what);
  bool IsKw(const Token& t, const char* kw) const;

  unique_ptr<Stmt> ParseCreate();
  unique_ptr<Stmt> ParseDrop();
  unique_ptr<Stmt> ParseSelect();
  unique_ptr<Stmt> ParseInsert();
  unique_ptr<Stmt> ParseDelete();
  unique_ptr<Stmt> ParseUpdate();

  unique_ptr<Expr> ParseExpr();
  unique_ptr<Expr> ParseOr();
  unique_ptr<Expr> ParseAnd();
  unique_ptr<Expr> ParseComparison();
  unique_ptr<Expr> ParsePrimary();
  unique_ptr<Expr> ParseColumnRefOrLiteral();

  vector<Token> tokens_;
  size_t pos_ = 0;
};

} // namespace minidb