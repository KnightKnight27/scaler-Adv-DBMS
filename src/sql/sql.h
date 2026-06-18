#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/value.h"

namespace minidb {

// ===========================================================================
// SQL front-end: tokens, expression tree, and statement AST, plus the Lexer
// and recursive-descent Parser. MiniDB supports a deliberately small dialect:
//   CREATE TABLE t (c TYPE, ...)
//   CREATE INDEX i ON t (c)
//   INSERT INTO t VALUES (..), (..)
//   SELECT a, b | * FROM t [JOIN u ON t.x = u.y] [WHERE pred]
//   DELETE FROM t [WHERE pred]
// pred is a conjunction/disjunction of comparisons over columns and literals.
// ===========================================================================

enum class TokenType { kKeyword, kIdentifier, kNumber, kString, kPunct, kEnd };

struct Token {
  TokenType type;
  std::string text;  // lexeme (keywords are upper-cased)
};

// ---- expression tree -------------------------------------------------------
enum class ExprKind { kColumn, kConst, kBinary };
enum class BinOp { kEq, kNe, kLt, kLe, kGt, kGe, kAnd, kOr };

struct Expr {
  ExprKind kind;

  // kColumn — optional `table` qualifier; `index` is filled in at bind time.
  std::string col_table;
  std::string col_name;
  int index{-1};

  // kConst
  Value value;

  // kBinary
  BinOp op{};
  std::unique_ptr<Expr> left;
  std::unique_ptr<Expr> right;
};
using ExprPtr = std::unique_ptr<Expr>;

// ---- statements ------------------------------------------------------------
enum class StmtType { kCreateTable, kCreateIndex, kInsert, kSelect, kDelete };

struct ColumnDef {
  std::string name;
  TypeId type;
  uint32_t length;  // VARCHAR length
};

struct Statement {
  StmtType type;

  // CREATE TABLE
  std::string table;
  std::vector<ColumnDef> columns;

  // CREATE INDEX
  std::string index_name;
  std::string index_column;

  // INSERT  (table reused)
  std::vector<std::vector<Value>> rows;

  // SELECT
  bool select_star{false};
  std::vector<std::string> select_list;  // qualified or bare names
  std::string from_table;
  bool has_join{false};
  std::string join_table;
  ExprPtr join_on;
  ExprPtr where;  // also used by DELETE
};
using StmtPtr = std::unique_ptr<Statement>;

// ---- lexer -----------------------------------------------------------------
class Lexer {
 public:
  explicit Lexer(std::string sql) : sql_(std::move(sql)) {}
  std::vector<Token> Tokenize();

 private:
  std::string sql_;
};

// ---- parser ----------------------------------------------------------------
class Parser {
 public:
  // Parse a single statement (trailing ';' optional). Throws on syntax errors.
  static StmtPtr Parse(const std::string &sql);

 private:
  explicit Parser(std::vector<Token> tokens) : toks_(std::move(tokens)) {}

  StmtPtr ParseStatement();
  StmtPtr ParseCreate();
  StmtPtr ParseInsert();
  StmtPtr ParseSelect();
  StmtPtr ParseDelete();

  ExprPtr ParseExpr();      // OR-level
  ExprPtr ParseAndExpr();   // AND-level
  ExprPtr ParseComparison();
  ExprPtr ParsePrimary();   // column ref or literal
  Value ParseLiteral();

  const Token &Peek() const { return toks_[pos_]; }
  const Token &Next() { return toks_[pos_++]; }
  bool Check(const std::string &text) const;       // matches keyword/punct text
  bool Accept(const std::string &text);            // consume if matches
  void Expect(const std::string &text);            // consume or throw
  bool AtEnd() const { return Peek().type == TokenType::kEnd; }

  std::vector<Token> toks_;
  size_t pos_{0};
};

}  // namespace minidb
