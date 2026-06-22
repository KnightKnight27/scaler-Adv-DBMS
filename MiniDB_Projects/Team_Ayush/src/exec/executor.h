#pragma once
#include <string>
#include <vector>
#include "engine/database.h"
#include "exec/operators.h"

namespace minidb {

// Outcome of executing one SQL statement.
struct ExecResult {
  bool        ok = true;
  std::string error;       // populated when ok == false
  std::string message;     // e.g. "1 row inserted", "Table created"

  bool                     is_query = false;   // SELECT produced a result set
  std::vector<std::string> columns;            // result column headers
  std::vector<Row>         rows;               // result rows

  bool        is_explain = false;
  std::string plan;        // EXPLAIN plan text
};

// Parse and execute a single SQL statement against the database.
ExecResult Execute(Database& db, const std::string& sql);

}  // namespace minidb
