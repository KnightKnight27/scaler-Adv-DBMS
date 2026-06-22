#pragma once

#include <string>

#include "parser/ast.h"

namespace axiomdb {

// ---------------------------------------------------------------------------
// Public parser entry point.  Parses exactly ONE SQL statement (an optional
// trailing ';' is allowed) into an AST.  On a syntax error it returns a null
// statement and a human-readable message (the REPL prints this rather than
// crashing -- malformed SQL must never take the engine down).
// ---------------------------------------------------------------------------

struct ParseResult {
  StmtPtr statement;   // null on error
  std::string error;   // empty on success

  bool ok() const { return statement != nullptr; }
};

ParseResult parse_sql(const std::string& sql);

// Helper for tests / EXPLAIN: render an expression back to a readable string.
std::string expr_to_string(const Expr* e);

}  // namespace axiomdb
