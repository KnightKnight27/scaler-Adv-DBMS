#pragma once
#include <memory>
#include <string>
#include <vector>
#include "record/schema.h"
#include "record/tuple.h"
#include "sql/ast.h"
#include "storage/row_store.h"

namespace minidb {

// --- schema / predicate helpers -------------------------------------------

// Produce a copy of `schema` with every column renamed "table.col" so columns
// stay unambiguous after joins.
Schema QualifySchema(const Schema &schema, const std::string &table);

// Resolve a (possibly qualified) column reference against `schema`. Throws on
// unknown or ambiguous references. Returns the column index.
int ResolveColumn(const Schema &schema, const std::string &ref);

// A WHERE conjunct bound to a concrete column position in some schema.
struct BoundPredicate {
  int       col;
  CompareOp op;
  Value     value;
};

std::vector<BoundPredicate> BindPredicates(const Schema &schema,
                                           const std::vector<Predicate> &preds);
bool EvalPredicates(const Tuple &t, const std::vector<BoundPredicate> &preds);

// --- Volcano operators -----------------------------------------------------
// Every operator implements Init() + Next(); the root is pulled until Next()
// returns false. Init() may be called repeatedly to rewind (used by joins).

class Executor {
 public:
  virtual ~Executor() = default;
  virtual void Init() = 0;
  virtual bool Next(Tuple *out) = 0;
  virtual const Schema &OutputSchema() const = 0;
};

// Full table scan via RowStore::ScanAll().
class SeqScanExecutor : public Executor {
 public:
  SeqScanExecutor(RowStore *store, Schema out) : store_(store), out_(std::move(out)) {}
  void Init() override { cursor_ = store_->ScanAll(); }
  bool Next(Tuple *out) override { return cursor_ && cursor_->Next(out); }
  const Schema &OutputSchema() const override { return out_; }

 private:
  RowStore                  *store_;
  Schema                     out_;
  std::unique_ptr<RowCursor> cursor_;
};

// Primary-key range scan via RowStore::RangeScan() — the index access path.
class IndexScanExecutor : public Executor {
 public:
  IndexScanExecutor(RowStore *store, Schema out, int64_t low, int64_t high)
      : store_(store), out_(std::move(out)), low_(low), high_(high) {}
  void Init() override { cursor_ = store_->RangeScan(low_, high_); }
  bool Next(Tuple *out) override { return cursor_ && cursor_->Next(out); }
  const Schema &OutputSchema() const override { return out_; }

 private:
  RowStore                  *store_;
  Schema                     out_;
  int64_t                    low_, high_;
  std::unique_ptr<RowCursor> cursor_;
};

// Applies a conjunction of predicates to its child stream.
class FilterExecutor : public Executor {
 public:
  FilterExecutor(std::unique_ptr<Executor> child, std::vector<BoundPredicate> preds)
      : child_(std::move(child)), preds_(std::move(preds)) {}
  void Init() override { child_->Init(); }
  bool Next(Tuple *out) override;
  const Schema &OutputSchema() const override { return child_->OutputSchema(); }

 private:
  std::unique_ptr<Executor>   child_;
  std::vector<BoundPredicate> preds_;
};

// Projects a subset of its child's columns.
class ProjectionExecutor : public Executor {
 public:
  ProjectionExecutor(std::unique_ptr<Executor> child, std::vector<int> cols, Schema out)
      : child_(std::move(child)), cols_(std::move(cols)), out_(std::move(out)) {}
  void Init() override { child_->Init(); }
  bool Next(Tuple *out) override;
  const Schema &OutputSchema() const override { return out_; }

 private:
  std::unique_ptr<Executor> child_;
  std::vector<int>          cols_;
  Schema                    out_;
};

// Nested-loop equi-join: rescans the inner child for each outer row.
class NestedLoopJoinExecutor : public Executor {
 public:
  NestedLoopJoinExecutor(std::unique_ptr<Executor> left, std::unique_ptr<Executor> right,
                         int left_col, int right_col, Schema out)
      : left_(std::move(left)), right_(std::move(right)),
        left_col_(left_col), right_col_(right_col), out_(std::move(out)) {}
  void Init() override;
  bool Next(Tuple *out) override;
  const Schema &OutputSchema() const override { return out_; }

 private:
  std::unique_ptr<Executor> left_, right_;
  int                       left_col_, right_col_;
  Schema                    out_;
  Tuple                     left_row_;
  bool                      have_left_{false};
};

// COUNT(*): consumes its child and emits a single one-column row.
class CountExecutor : public Executor {
 public:
  explicit CountExecutor(std::unique_ptr<Executor> child)
      : child_(std::move(child)),
        out_(Schema({{"count", TypeId::INTEGER, false}})) {}
  void Init() override { child_->Init(); done_ = false; }
  bool Next(Tuple *out) override;
  const Schema &OutputSchema() const override { return out_; }

 private:
  std::unique_ptr<Executor> child_;
  Schema                    out_;
  bool                      done_{false};
};

}  // namespace minidb
