#pragma once
#include <string>
#include <vector>
#include "catalog/catalog.h"
#include "common/types.h"
#include "plan/optimizer.h"
#include "sql/ast.h"

namespace minidb {

// The outcome of running one SQL statement.
struct QueryResult {
  bool                     is_select = false;
  std::vector<std::string> columns;  // column headers (SELECT only)
  std::vector<Row>         rows;       // result rows (SELECT only)
  std::string              message;    // e.g. "INSERT 1", "DELETE 3", or EXPLAIN text
};

// Runs SQL end-to-end: lex -> parse -> (optimize -> execute). Reads and writes
// tables through the Catalog's storage engines. Throws std::runtime_error on
// any lexing/parsing/execution error.
class Executor {
 public:
  explicit Executor(Catalog& catalog) : catalog_(catalog), optimizer_(catalog) {}

  QueryResult run(const std::string& sql);

 private:
  QueryResult run_select(const SelectStmt& stmt, bool explain);
  QueryResult run_insert(const InsertStmt& stmt);
  QueryResult run_delete(const DeleteStmt& stmt);

  Catalog&  catalog_;
  Optimizer optimizer_;
};

// Formats a single value for display.
std::string value_to_string(const Value& v);

}  // namespace minidb
