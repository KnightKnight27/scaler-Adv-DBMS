#pragma once
#include <string>
#include <vector>
#include "sql/ast.h"

namespace minidb {

// Token kinds produced by the lexer.
enum class Tok {
  Ident, IntLit, StrLit,
  LParen, RParen, Comma, Semicolon, Star, Dot,
  Eq, Ne, Lt, Le, Gt, Ge,
  // keywords
  Create, Table, Insert, Into, Values, Select, From, Where, Join, On,
  Delete, And, PrimaryKey, KwInteger, KwVarchar, Count,
  Begin, Commit, Abort, Rollback, Using,
  End
};

struct Token {
  Tok kind;
  std::string text;   // identifier / string literal contents
  int64_t int_val = 0;
};

// Recursive-descent parser for MiniDB's SQL subset:
//   CREATE TABLE t (col TYPE [PRIMARY KEY], ...)
//   INSERT INTO t VALUES (v, ...)
//   SELECT */cols/COUNT(*) FROM t [JOIN t2 ON a=b] [WHERE p AND ...]
//   DELETE FROM t [WHERE p AND ...]
class Parser {
 public:
  explicit Parser(const std::string& sql);
  Statement Parse();  // throws DBError on syntax error

 private:
  std::vector<Token> Tokenize(const std::string& sql);

  const Token& Peek() const { return tokens_[pos_]; }
  const Token& Advance() { return tokens_[pos_++]; }
  bool Check(Tok k) const { return tokens_[pos_].kind == k; }
  bool Match(Tok k);
  const Token& Expect(Tok k, const char* what);

  Statement ParseCreate();
  Statement ParseInsert();
  Statement ParseSelect();
  Statement ParseDelete();
  ColRef ParseColRef();
  Predicate ParsePredicate();
  std::vector<Predicate> ParseWhere();
  Value ParseLiteral();

  std::vector<Token> tokens_;
  size_t pos_ = 0;
};

}  // namespace minidb
