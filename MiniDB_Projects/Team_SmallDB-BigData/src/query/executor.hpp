#pragma once

#include <iosfwd>

#include "ast.hpp"

class Database;

// run one statement; output goes to `out`
void execute_statement(const Statement& stmt, Database& db, std::ostream& out);
