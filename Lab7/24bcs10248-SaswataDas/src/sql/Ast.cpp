#include "sql/Ast.h"

#include <stdexcept>

namespace sql {

bool Comparison::eval(const Row& row) const {
    const Value& cell = row.at(column_);
    switch (op_) {
        case CompOp::Eq:
            return valueEqual(cell, literal_);
        case CompOp::Ne:
            return !valueEqual(cell, literal_);
        case CompOp::Lt:
            return valueLess(cell, literal_);
        case CompOp::Le:
            return valueLess(cell, literal_) || valueEqual(cell, literal_);
        case CompOp::Gt:
            return valueLess(literal_, cell);
        case CompOp::Ge:
            return valueLess(literal_, cell) || valueEqual(cell, literal_);
    }
    throw std::runtime_error("unhandled comparison operator");
}

bool Logical::eval(const Row& row) const {
    // Short-circuit, mirroring SQL boolean evaluation.
    if (op_ == LogicOp::And) {
        return lhs_->eval(row) && rhs_->eval(row);
    }
    return lhs_->eval(row) || rhs_->eval(row);
}

}  // namespace sql
