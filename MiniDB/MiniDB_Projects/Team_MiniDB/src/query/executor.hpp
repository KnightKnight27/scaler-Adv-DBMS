#pragma once

#include <iosfwd>

#include "ast.hpp"

class Catalog;

// Execute one parsed statement against the catalog. SELECT results and status
// lines ("1 row inserted", etc.) are written to `out`. Throws std::runtime_error
// on a semantic error (unknown table/column, type mismatch, duplicate key, ...).
void execute_statement(const Statement& stmt, Catalog& catalog, std::ostream& out);
