// ============================================================================
// parser.h  --  A recursive-descent parser that builds an AST from tokens.
//
// "Recursive descent" means: one function per grammar rule, and rules that
// contain other rules call the matching functions.  parseStatement looks at the
// first keyword and dispatches to parseSelect / parseInsert / etc.  Each of
// those consumes tokens left to right, throwing a ParseError on anything
// unexpected.  This style is easy to read and easy to extend.
// ============================================================================
#pragma once

#include "common/common.h"
#include "sql/ast.h"
#include "sql/lexer.h"

namespace minidb {

class Parser {
 public:
  // parse one SQL statement string into a Statement (throws ParseError if bad).
  static unique_ptr<Statement> parse(const string &sql);

 private:
  explicit Parser(vector<Token> toks) : toks_(move(toks)) {}

  unique_ptr<Statement> parseStatement();
  unique_ptr<Statement> parseCreate();
  unique_ptr<Statement> parseInsert();
  unique_ptr<Statement> parseSelect();
  unique_ptr<Statement> parseDelete();

  // Shared sub-rules.
  vector<Predicate> parseWhere();             // [WHERE p AND p AND ...]
  Predicate              parsePredicate();         // colref op value
  pair<string, string> parseColRef(); // (table, col) or ("",col)
  Value                  parseValue();             // number or 'string'

  // --- token cursor helpers ---
  const Token &peek() const { return toks_[pos_]; }
  const Token &next() { return toks_[pos_++]; }
  bool isKeyword(const string &kw) const;      // case-insensitive ident
  bool acceptKeyword(const string &kw);        // consume if it matches
  void expectKeyword(const string &kw);        // consume or throw
  bool isSymbol(const string &s) const;
  bool acceptSymbol(const string &s);
  void expectSymbol(const string &s);
  string expectIdent();

  vector<Token> toks_;
  size_t             pos_{0};
};

}  // namespace minidb
