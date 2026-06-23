#pragma once
// Recursive-descent parser producing a single Statement from SQL text.
#include "minidb/sql/ast.hpp"
#include "minidb/sql/lexer.hpp"
#include <string>
#include <vector>

namespace minidb {

class Parser {
 public:
  explicit Parser(std::string sql);
  Statement parse();  // throws std::runtime_error on syntax errors

 private:
  const Token& peek() const { return toks_[pos_]; }
  const Token& advance() { return toks_[pos_++]; }
  bool check(TokKind k) const { return peek().kind == k; }
  bool at_keyword(const char* kw) const;
  const Token& expect(TokKind k, const char* what);
  void expect_keyword(const char* kw);

  CreateTableStmt parse_create();
  CreateIndexStmt parse_create_index();
  InsertStmt parse_insert();
  SelectStmt parse_select();
  DeleteStmt parse_delete();
  WhereClause parse_where();
  std::vector<std::string> parse_order_by();
  std::string parse_column_ref();  // ident or ident '.' ident
  Value parse_literal();
  CmpOp parse_cmp();

  std::vector<Token> toks_;
  size_t pos_ = 0;
};

}  // namespace minidb
