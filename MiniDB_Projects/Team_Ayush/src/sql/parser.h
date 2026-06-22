#pragma once
#include <string>
#include "sql/ast.h"

namespace minidb {

struct ParseResult {
  bool        ok = false;
  std::string error;
  Statement   stmt;
};

// Parse one SQL statement (trailing ';' optional). On failure ok=false and
// `error` describes the problem.
ParseResult Parse(const std::string& sql);

}  // namespace minidb
