// MiniDB - expression evaluator: walks a WHERE/predicate Expr tree over one tuple.
// This is the tree-walk evaluator from Lab 5, lifted to typed Values and a real schema.
#pragma once

#include "../common/schema.h"
#include "../common/tuple.h"
#include "../common/types.h"
#include "../parser/ast.h"

namespace minidb {

class Evaluator {
public:
    // Evaluate an expression to a Value (comparisons/logic yield BOOLEAN).
    static Value Eval(const Expr* e, const Tuple& row, const Schema& schema);
    // Convenience for WHERE clauses. A null predicate means "always true".
    static bool Matches(const Expr* pred, const Tuple& row, const Schema& schema) {
        return pred == nullptr || Eval(pred, row, schema).AsBool();
    }
};

}  // namespace minidb
