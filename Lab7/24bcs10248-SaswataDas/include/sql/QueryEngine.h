#pragma once

#include "Ast.h"
#include "Table.h"

namespace sql {

// Executes a parsed SelectStatement against an in-memory table and returns
// the filtered, projected result as a new table. It collaborates with the
// WHERE clause purely through the BoolExpr abstraction.
class QueryEngine {
public:
    Table execute(const SelectStatement& statement, const Table& input) const;
};

}  // namespace sql
