#include "plan_node.h"

namespace minidb {

bool EvalPredicate(const ResolvedPredicate &p, const Row &row) {
  const Value &lhs = row.values[p.left_idx];
  Value rhs = p.right_is_col ? row.values[p.right_idx] : p.right_literal;
  switch (p.op) {
    case CompareOp::EQ: return lhs == rhs;
    case CompareOp::NEQ: return !(lhs == rhs);
    case CompareOp::LT: return lhs < rhs;
    case CompareOp::LTE: return lhs < rhs || lhs == rhs;
    case CompareOp::GT: return rhs < lhs;
    case CompareOp::GTE: return rhs < lhs || lhs == rhs;
  }
  return false;
}

}  // namespace minidb
