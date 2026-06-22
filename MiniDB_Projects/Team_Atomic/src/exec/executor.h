#pragma once
#include <memory>
#include <string>
#include <vector>
#include "common/types.h"
#include "sql/ast.h"
#include "record/schema.h"
#include "storage/row_store.h"

namespace minidb {

// A materialized output column: where it came from + its type.
struct ColumnInfo {
  std::string table;
  std::string name;
  TypeId type;
};
using OutSchema = std::vector<ColumnInfo>;
using Row = std::vector<Value>;

// A WHERE/JOIN predicate bound to positions in a concrete OutSchema.
struct BoundPredicate {
  int lhs_idx;
  CmpOp op;
  bool rhs_is_col;
  int rhs_idx;     // valid if rhs_is_col
  Value rhs_val;   // valid otherwise
};

// Resolve a column reference against a schema; throws on missing/ambiguous.
int ResolveColumn(const OutSchema& schema, const ColRef& ref);
bool EvalPredicate(const Row& row, const BoundPredicate& p);
BoundPredicate BindPredicate(const OutSchema& schema, const Predicate& p);

// ---- Volcano iterator model: every operator is Init() + Next() ----
class Executor {
 public:
  virtual ~Executor() = default;
  virtual void Init() = 0;
  virtual bool Next(Row* out) = 0;
  virtual const OutSchema& GetSchema() const = 0;
};

// Sequential scan over a table heap. `counter` (if set) is bumped per page-row
// examined so benchmarks/optimizer demos can report rows scanned.
class SeqScanExecutor : public Executor {
 public:
  SeqScanExecutor(RowStore* store, Schema table_schema, std::string alias);
  void Init() override;
  bool Next(Row* out) override;
  const OutSchema& GetSchema() const override { return out_schema_; }

 private:
  RowStore* store_;
  Schema table_schema_;
  OutSchema out_schema_;
  std::unique_ptr<RowCursor> cursor_;
};

// Index/range scan: ask the store for rows in a key range (B+ tree range walk
// for the heap engine, sorted-run merge for the LSM engine).
class IndexScanExecutor : public Executor {
 public:
  IndexScanExecutor(RowStore* store, Schema table_schema, std::string alias,
                    int64_t low, int64_t high);
  void Init() override;
  bool Next(Row* out) override;
  const OutSchema& GetSchema() const override { return out_schema_; }

 private:
  RowStore* store_;
  Schema table_schema_;
  OutSchema out_schema_;
  int64_t low_, high_;
  std::unique_ptr<RowCursor> cursor_;
};

// Filter: emit child rows that satisfy every bound predicate.
class FilterExecutor : public Executor {
 public:
  FilterExecutor(std::unique_ptr<Executor> child, std::vector<BoundPredicate> preds);
  void Init() override;
  bool Next(Row* out) override;
  const OutSchema& GetSchema() const override { return child_->GetSchema(); }

 private:
  std::unique_ptr<Executor> child_;
  std::vector<BoundPredicate> preds_;
};

// Nested-loop join: for each left row, rescan the right child.
class NestedLoopJoinExecutor : public Executor {
 public:
  NestedLoopJoinExecutor(std::unique_ptr<Executor> left,
                         std::unique_ptr<Executor> right, Predicate on);
  void Init() override;
  bool Next(Row* out) override;
  const OutSchema& GetSchema() const override { return out_schema_; }

 private:
  std::unique_ptr<Executor> left_, right_;
  Predicate on_;
  BoundPredicate bound_;
  OutSchema out_schema_;
  Row left_row_;
  bool have_left_ = false;
};

// Projection: keep only selected columns (by resolved index).
class ProjectionExecutor : public Executor {
 public:
  ProjectionExecutor(std::unique_ptr<Executor> child, const std::vector<ColRef>& cols);
  void Init() override;
  bool Next(Row* out) override;
  const OutSchema& GetSchema() const override { return out_schema_; }

 private:
  std::unique_ptr<Executor> child_;
  std::vector<int> indexes_;
  OutSchema out_schema_;
};

// COUNT(*): consume the child, emit a single one-column row.
class CountExecutor : public Executor {
 public:
  explicit CountExecutor(std::unique_ptr<Executor> child);
  void Init() override;
  bool Next(Row* out) override;
  const OutSchema& GetSchema() const override { return out_schema_; }

 private:
  std::unique_ptr<Executor> child_;
  OutSchema out_schema_;
  bool done_ = false;
};

}  // namespace minidb
