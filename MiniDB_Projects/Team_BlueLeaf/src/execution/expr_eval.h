#pragma once

#include <string>

#include "common/types.h"
#include "execution/iterator.h"
#include "parser/ast.h"

namespace minidb {

// Resolve a column reference (optionally table-qualified) to its index in an
// output schema. Throws if not found or ambiguous.
int resolve_column(const OutSchema& schema, const std::string& table, const std::string& name);

// Evaluate a literal or column expression to a Value.
Value eval_scalar(const Expr* e, const Tuple& t, const OutSchema& schema);

// Evaluate a boolean predicate (comparison, AND, OR) over a tuple.
bool eval_predicate(const Expr* e, const Tuple& t, const OutSchema& schema);

} // namespace minidb
