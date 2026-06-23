#pragma once
#include <vector>
#include "sql/ast.h"
#include "sql/token.h"

namespace minidb {

// Recursive-descent parser. Grammar (minimal SQL):
//   statement := [EXPLAIN] (select | insert | delete)
//   select    := SELECT (* | col {, col}) FROM ident [JOIN ident ON expr] [WHERE expr]
//   insert    := INSERT INTO ident VALUES ( literal {, literal} )
//   delete    := DELETE FROM ident [WHERE expr]
//   expr      := and {OR and}
//   and       := cmp {AND cmp}
//   cmp       := primary [op primary]          op in = != < > <= >=
//   primary   := ( expr ) | col | number | string
// Expression precedence is encoded by the call chain (OR lowest, comparisons
// highest), so no separate precedence table is needed.
class Parser {
 public:
  explicit Parser(std::vector<Token> tokens) : toks_(std::move(tokens)) {}

  Statement parse();           // parses exactly one statement
  bool      explain() const { return explain_; }  // was it prefixed with EXPLAIN?

 private:
  const Token& peek() const { return toks_[pos_]; }
  const Token& advance() { return toks_[pos_++]; }
  bool         match(TokenType type);
  const Token& expect(TokenType type, const char* what);

  SelectStmt parse_select();
  InsertStmt parse_insert();
  DeleteStmt parse_delete();

  ExprPtr parse_expr();        // OR level
  ExprPtr parse_and();         // AND level
  ExprPtr parse_comparison();
  ExprPtr parse_primary();
  ColumnRef parse_column_ref();

  std::vector<Token> toks_;
  std::size_t        pos_     = 0;
  bool               explain_ = false;
};

}  // namespace minidb
