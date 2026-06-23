#include "execution/execution.h"

#include "common/exception.h"

namespace minidb {

Value EvalScalar(const Expr *e, const std::vector<Value> &row) {
  switch (e->kind) {
    case ExprKind::kColumn:
      return row[e->index];
    case ExprKind::kConst:
      return e->value;
    default:
      throw Exception(ErrorKind::kExecution, "non-scalar expression used as a value");
  }
}

bool EvalPredicate(const Expr *e, const std::vector<Value> &row) {
  if (e->kind != ExprKind::kBinary) {
    // A bare column/const predicate is truthy if non-zero / non-empty.
    Value v = EvalScalar(e, row);
    return v.GetType() == TypeId::INTEGER ? v.GetInt() != 0 : !v.GetString().empty();
  }
  switch (e->op) {
    case BinOp::kAnd:
      return EvalPredicate(e->left.get(), row) && EvalPredicate(e->right.get(), row);
    case BinOp::kOr:
      return EvalPredicate(e->left.get(), row) || EvalPredicate(e->right.get(), row);
    default:
      break;
  }
  // comparison
  int c = EvalScalar(e->left.get(), row).Compare(EvalScalar(e->right.get(), row));
  switch (e->op) {
    case BinOp::kEq: return c == 0;
    case BinOp::kNe: return c != 0;
    case BinOp::kLt: return c < 0;
    case BinOp::kLe: return c <= 0;
    case BinOp::kGt: return c > 0;
    case BinOp::kGe: return c >= 0;
    default: throw Exception(ErrorKind::kExecution, "bad operator in predicate");
  }
}

bool SeqScanExecutor::Next(std::vector<Value> *row) {
  if (!(it_ != heap_->End())) return false;
  Tuple t = *it_;
  *row = t.GetValues(*schema_);
  ++it_;
  return true;
}

bool FilterExecutor::Next(std::vector<Value> *row) {
  std::vector<Value> candidate;
  while (child_->Next(&candidate)) {
    if (EvalPredicate(pred_, candidate)) {
      *row = std::move(candidate);
      return true;
    }
  }
  return false;
}

bool ProjectionExecutor::Next(std::vector<Value> *row) {
  std::vector<Value> in;
  if (!child_->Next(&in)) return false;
  row->clear();
  row->reserve(cols_.size());
  for (int idx : cols_) row->push_back(in[idx]);
  return true;
}

void NestedLoopJoinExecutor::Init() {
  left_->Init();
  right_->Init();
  have_left_ = left_->Next(&left_row_);
}

bool NestedLoopJoinExecutor::Next(std::vector<Value> *row) {
  while (have_left_) {
    std::vector<Value> right_row;
    while (right_->Next(&right_row)) {
      std::vector<Value> combined = left_row_;
      combined.insert(combined.end(), right_row.begin(), right_row.end());
      if (on_ == nullptr || EvalPredicate(on_, combined)) {
        *row = std::move(combined);
        return true;
      }
    }
    // Right side exhausted for this left row: advance left, rescan right.
    have_left_ = left_->Next(&left_row_);
    right_->Init();
  }
  return false;
}

}  // namespace minidb
