#pragma once
#include "parser.h"
#include "../catalog/catalog.h"
#include "../storage/heap_file.h"
#include <vector>
#include <string>

// A result row: a list of (column_name, value_string) pairs.
// We use strings for everything so printing is uniform.
struct ResultRow {
    std::vector<std::string> col_names;
    std::vector<std::string> values;
};

// The Executor takes a parsed statement, accesses the catalog,
// and returns the result rows (for SELECT) or modifies the database (for INSERT/DELETE).
class Executor {
public:
    explicit Executor(Catalog& catalog);

    // Run a SELECT. Returns matching rows.
    std::vector<ResultRow> executeSelect(SelectStmt* stmt);

    // Run an INSERT. Returns the inserted row.
    Row executeInsert(InsertStmt* stmt);

    // Run a DELETE. Returns how many rows were deleted.
    int executeDelete(DeleteStmt* stmt);

private:
    Catalog& cat;

    // Evaluate a WHERE expression against one row.
    // 'table_prefix' is used to resolve "table.column" references.
    bool evalExpr(Expr* expr, const Row& row, const std::string& table_prefix);

    // Get the integer value of a column from a row.
    int getIntCol(const std::string& col, const Row& row);

    // Project a row into the requested columns.
    ResultRow project(const std::vector<std::string>& cols,
                      const Row& row,
                      const std::string& table_prefix);

    // Make a result row from two rows (for JOIN output).
    ResultRow projectJoin(const std::vector<std::string>& cols,
                          const Row& outer, const std::string& outer_table,
                          const Row& inner, const std::string& inner_table);
};
