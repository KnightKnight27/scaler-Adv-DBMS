#pragma once

#include <iosfwd>

#include "ast.hpp"

class Database;

// Execute one parsed statement against the database (catalog + WAL + current
// transaction). SELECT results and status lines ("1 row inserted", etc.) are
// written to `out`. Throws std::runtime_error on a semantic error (unknown
// table/column, type mismatch, duplicate key, ...).
void execute_statement(const Statement& stmt, Database& db, std::ostream& out);
