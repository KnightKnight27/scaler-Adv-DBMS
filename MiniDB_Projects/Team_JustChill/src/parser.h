// parser.h — Track 3 (Query & Concurrency): SQL front-end
//
// A small, hand-written SQL front-end in two stages:
//
//   1. Lexer (`tokenize`)  — raw SQL text -> a flat vector of Tokens
//      (keywords, identifiers, qualified `table.column`, integer/string
//      literals, comparison operators, punctuation). Case-insensitive
//      keywords; single-quoted string literals.
//
//   2. Parser (`Parser::parse`) — tokens -> a typed AST (`Statement`) for the
//      supported grammar:
//
//        [EXPLAIN] SELECT <select-list> FROM <table>
//                  [JOIN <table> ON <col> = <col>]
//                  [WHERE <bool-expr>]
//        [EXPLAIN] INSERT INTO <table> VALUES (<literal>, ...)
//        [EXPLAIN] DELETE FROM <table> [WHERE <bool-expr>]
//
//      <select-list> ::= '*' | <col> (',' <col>)*
//      <bool-expr>   ::= <or>; <or> ::= <and> ('OR' <and>)*;
//                        <and> ::= <cmp> ('AND' <cmp>)*;
//                        <cmp> ::= '(' <or> ')' | <col> <op> <literal>
//                        (AND binds tighter than OR)
//      <op>          ::= '=' | '!=' | '<>' | '<' | '<=' | '>' | '>='
//
// The AST is consumed by the cost-based optimizer (optimizer.h), which turns
// it into a physical operator tree. Any malformed input throws
// std::runtime_error with a human-readable message.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "execution.h"  // ValueType, CompareOp, Value

namespace minidb {

// ---- Lexer ----

enum class TokenType {
  Keyword,  // SELECT, FROM, WHERE, INSERT, INTO, VALUES, DELETE, JOIN, ON, EXPLAIN
  Ident,    // table / column name
  IntLit,   // 123, -7
  StrLit,   // 'text'
  Op,       // = != <> < <= > >=
  Comma,    // ,
  Dot,      // .
  LParen,   // (
  RParen,   // )
  Star,     // *
  End       // end of input
};

struct Token {
  TokenType type = TokenType::End;
  std::string text;     // canonical spelling (keywords upper-cased)
  int64_t int_val = 0;  // valid when type == IntLit
};

// Split SQL text into tokens. Throws std::runtime_error on an illegal
// character or an unterminated string literal.
std::vector<Token> tokenize(const std::string& sql);

// ---- AST ----

// A column reference, optionally qualified by a table name (`t.col`).
struct ColumnRef {
  std::string table;   // empty when unqualified
  std::string column;
};

// A literal value appearing in VALUES(...) or on the right of a WHERE.
struct LiteralVal {
  ValueType type = ValueType::Int;
  int64_t i = 0;
  std::string s;

  Value toValue() const {
    return type == ValueType::Int ? Value::Int(i) : Value::Text(s);
  }
};

// `column <op> literal` — a single comparison, the leaf of a WHERE expression.
struct Condition {
  ColumnRef col;
  CompareOp op = CompareOp::Eq;
  LiteralVal val;
};

// A WHERE clause as a boolean expression tree: comparison leaves combined with
// AND/OR. Column references are still by name here; the optimizer resolves them
// to indices and produces the executable PredExpr (execution.h). A null
// `where` pointer means the clause was omitted.
struct WhereExpr {
  enum class Kind { Leaf, And, Or };
  Kind kind = Kind::Leaf;
  Condition leaf;                          // valid when kind == Leaf
  std::shared_ptr<WhereExpr> left, right;  // valid when kind == And/Or
};

// JOIN <table> ON <left> = <right>  (equi-join only).
struct JoinClause {
  std::string table;
  ColumnRef left;
  ColumnRef right;
};

struct SelectStatement {
  bool star = false;                  // SELECT *
  std::vector<ColumnRef> columns;     // projection list when !star
  std::string from;
  std::optional<JoinClause> join;
  std::shared_ptr<WhereExpr> where;   // null when no WHERE
};

struct InsertStatement {
  std::string table;
  std::vector<LiteralVal> values;
};

struct DeleteStatement {
  std::string table;
  std::shared_ptr<WhereExpr> where;   // null when no WHERE
};

// A parsed statement. `explain` is set when the query was prefixed with
// EXPLAIN, asking the planner to print its plan instead of executing.
struct Statement {
  bool explain = false;
  std::variant<SelectStatement, InsertStatement, DeleteStatement> node;

  bool isSelect() const { return std::holds_alternative<SelectStatement>(node); }
  bool isInsert() const { return std::holds_alternative<InsertStatement>(node); }
  bool isDelete() const { return std::holds_alternative<DeleteStatement>(node); }
};

// ---- Parser ----

class Parser {
 public:
  // Parse a single SQL statement. Throws std::runtime_error on any syntax
  // error (unknown keyword, missing clause, bad literal, trailing tokens).
  static Statement parse(const std::string& sql);
};

}  // namespace minidb
