// supported grammar:
//   CREATE TABLE t (col TYPE [PRIMARY KEY], ...)
//   INSERT INTO t [(cols)] VALUES (..)[, (..)]
//   SELECT <items|*> FROM t [a] [JOIN t2 [b] ON expr]* [WHERE expr]
//          [GROUP BY cols] [ORDER BY col [ASC|DESC]] [LIMIT n]
//   DELETE FROM t [WHERE expr]
//   BEGIN | COMMIT | ROLLBACK
#pragma once

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "common.h"

namespace minidb {

enum class ExprKind { Column, IntLit, StrLit, BoolLit, NullLit, Binary, Unary, Agg };

struct Expr {
  ExprKind kind;
  std::string col_name;
  int64_t int_val = 0;
  std::string str_val;
  bool bool_val = false;
  std::string op;
  std::shared_ptr<Expr> left;
  std::shared_ptr<Expr> right;
  std::string func;
  bool star = false;
};
using ExprPtr = std::shared_ptr<Expr>;

struct ColumnDef {
  std::string name;
  TypeId type;
  bool primary_key = false;
};
struct CreateTableStmt {
  std::string table;
  std::vector<ColumnDef> columns;
};

struct InsertStmt {
  std::string table;
  std::vector<std::string> columns;          // empty => all columns in order
  std::vector<std::vector<ExprPtr>> rows;
};

struct SelectItem {
  ExprPtr expr;
  std::string alias;
  bool star = false;
};
struct TableRef {
  std::string name;
  std::string alias;
};
struct JoinClause {
  TableRef right;
  ExprPtr on;
};
struct OrderByItem {
  std::string col;
  bool desc = false;
};
struct SelectStmt {
  std::vector<SelectItem> items;
  TableRef from;
  std::vector<JoinClause> joins;
  ExprPtr where;
  std::vector<std::string> group_by;
  std::vector<OrderByItem> order_by;
  bool has_limit = false;
  int64_t limit = 0;
};

struct DeleteStmt {
  std::string table;
  ExprPtr where;
};

enum class TxnCmd { Begin, Commit, Rollback };
struct TxnStmt {
  TxnCmd cmd;
};

using Statement = std::variant<CreateTableStmt, InsertStmt, SelectStmt, DeleteStmt, TxnStmt>;

struct Token {
  enum class Type { Ident, Keyword, Number, String, Punct, End };
  Type type;
  std::string text;  // keywords stored upper-cased
};

class Parser {
 public:
  static Statement parse(const std::string& sql);

 private:
  explicit Parser(std::vector<Token> toks) : toks_(std::move(toks)) {}

  static std::vector<Token> lex(const std::string& sql);

  Statement parse_statement();
  CreateTableStmt parse_create();
  InsertStmt parse_insert();
  SelectStmt parse_select();
  DeleteStmt parse_delete();

  ExprPtr parse_expr();
  ExprPtr parse_or();
  ExprPtr parse_and();
  ExprPtr parse_cmp();
  ExprPtr parse_add();
  ExprPtr parse_mul();
  ExprPtr parse_primary();

  const Token& peek() const { return toks_[pos_]; }
  const Token& next() { return toks_[pos_++]; }
  bool is_kw(const std::string& kw) const {
    return peek().type == Token::Type::Keyword && peek().text == kw;
  }
  bool is_punct(const std::string& p) const {
    return peek().type == Token::Type::Punct && peek().text == p;
  }
  bool accept_kw(const std::string& kw);
  bool accept_punct(const std::string& p);
  void expect_kw(const std::string& kw);
  void expect_punct(const std::string& p);
  std::string expect_ident();
  [[noreturn]] void fail(const std::string& msg) const;

  std::vector<Token> toks_;
  size_t pos_ = 0;
};

}  // namespace minidb
