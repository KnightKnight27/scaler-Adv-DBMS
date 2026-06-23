#pragma once
#include <stdexcept>
#include <string>
#include <vector>
#include "sql/ast.h"

namespace minidb {

// Thrown on any lexing/parsing error.
struct ParseError : std::runtime_error {
  explicit ParseError(const std::string &msg) : std::runtime_error(msg) {}
};

// A hand-written lexer + recursive-descent parser for MiniDB's SQL subset:
//
//   CREATE TABLE t (col INTEGER PRIMARY KEY, col VARCHAR, ...) [USING LSM];
//   INSERT INTO t VALUES (..., ...);
//   SELECT * | col,... | COUNT(*) FROM t [JOIN t2 ON t.a = t2.b]
//                                        [WHERE p AND p ...];
//   DELETE FROM t [WHERE p AND ...];
//   BEGIN; COMMIT; ROLLBACK;
class Parser {
 public:
  // Parse exactly one statement from `sql`.
  Statement Parse(const std::string &sql);

 private:
  enum class TokType { IDENT, NUMBER, STRING, PUNCT, END };
  struct Token {
    TokType     type;
    std::string text;  // for IDENT, uppercased keyword match is case-insensitive
  };

  void Tokenize(const std::string &sql);

  // Token cursor helpers.
  const Token &Peek() const { return tokens_[pos_]; }
  const Token &Next() { return tokens_[pos_++]; }
  bool IsKeyword(const std::string &kw) const;       // case-insensitive IDENT match
  bool AcceptKeyword(const std::string &kw);         // consume if matches
  void ExpectKeyword(const std::string &kw);
  void ExpectPunct(const std::string &p);
  bool AcceptPunct(const std::string &p);
  std::string ExpectIdent();

  // Productions.
  Statement ParseCreate();
  Statement ParseInsert();
  Statement ParseSelect();
  Statement ParseDelete();
  std::vector<Predicate> ParseWhere();
  Value ParseLiteral();

  std::vector<Token> tokens_;
  size_t             pos_{0};
};

}  // namespace minidb
