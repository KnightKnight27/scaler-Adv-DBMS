#pragma once
#include "common/types.h"
#include "sql/ast.h"

namespace minidb {

// Resolves a column reference to its position in a schema. Matches an exact
// "table.column" name first, then a bare column name, then any column whose
// name ends in ".column" (so unqualified refs work over a joined schema).
int resolve_column(const Schema& schema, const ColumnRef& ref);

// Evaluates a value expression (column / literal) against a row.
Value eval_value(const Expr* expr, const Row& row, const Schema& schema);

// Evaluates a boolean predicate (comparison / AND / OR) against a row.
bool eval_predicate(const Expr* expr, const Row& row, const Schema& schema);

// Three-way compare of two same-typed values (-1, 0, +1).
int compare_values(const Value& a, const Value& b);

}  // namespace minidb
