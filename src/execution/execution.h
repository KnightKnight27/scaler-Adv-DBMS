#pragma once

#include <memory>
#include <vector>

#include "catalog/schema.h"
#include "common/value.h"
#include "sql/sql.h"
#include "storage/table_heap.h"

namespace minidb {

// Evaluate an expression tree against a materialized row (column refs already
// bound to indices). Comparisons/AND/OR return bool; column/const return Value.
Value EvalScalar(const Expr *e, const std::vector<Value> &row);
bool EvalPredicate(const Expr *e, const std::vector<Value> &row);

// ---- Volcano-style iterator interface --------------------------------------
// Each executor pulls rows from its child(ren) one at a time via Next().
class Executor {
 public:
  virtual ~Executor() = default;
  virtual void Init() = 0;
  virtual bool Next(std::vector<Value> *row) = 0;
};

// Full scan of a heap file, decoding each live tuple into values.
class SeqScanExecutor : public Executor {
 public:
  SeqScanExecutor(TableHeap *heap, const Schema *schema)
      : heap_(heap), schema_(schema), it_(heap->Begin()) {}
  void Init() override { it_ = heap_->Begin(); }
  bool Next(std::vector<Value> *row) override;

 private:
  TableHeap *heap_;
  const Schema *schema_;
  TableHeap::Iterator it_;
};

// (IndexScanExecutor is added in M2 alongside the B+ tree.)

// Pass through only the rows satisfying `pred`.
class FilterExecutor : public Executor {
 public:
  FilterExecutor(std::unique_ptr<Executor> child, const Expr *pred)
      : child_(std::move(child)), pred_(pred) {}
  void Init() override { child_->Init(); }
  bool Next(std::vector<Value> *row) override;

 private:
  std::unique_ptr<Executor> child_;
  const Expr *pred_;
};

// Project a subset/reorder of the child's columns.
class ProjectionExecutor : public Executor {
 public:
  ProjectionExecutor(std::unique_ptr<Executor> child, std::vector<int> cols)
      : child_(std::move(child)), cols_(std::move(cols)) {}
  void Init() override { child_->Init(); }
  bool Next(std::vector<Value> *row) override;

 private:
  std::unique_ptr<Executor> child_;
  std::vector<int> cols_;
};

// Inner nested-loop join; emits left ++ right rows where `on` holds.
class NestedLoopJoinExecutor : public Executor {
 public:
  NestedLoopJoinExecutor(std::unique_ptr<Executor> left, std::unique_ptr<Executor> right,
                         const Expr *on)
      : left_(std::move(left)), right_(std::move(right)), on_(on) {}
  void Init() override;
  bool Next(std::vector<Value> *row) override;

 private:
  std::unique_ptr<Executor> left_;
  std::unique_ptr<Executor> right_;
  const Expr *on_;
  std::vector<Value> left_row_;
  bool have_left_{false};
};

}  // namespace minidb
