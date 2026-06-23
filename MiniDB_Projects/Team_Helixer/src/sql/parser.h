#pragma once
#include <memory>
#include <string>
#include "sql/ast.h"

namespace minidb {

// Parse a single SQL statement string into an AST node. Throws
// std::runtime_error with a human-readable message on a syntax error.
std::unique_ptr<Statement> ParseSQL(const std::string &sql);

} // namespace minidb
