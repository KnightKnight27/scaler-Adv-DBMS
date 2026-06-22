#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "catalog/catalog.h"
#include "common/types.h"
#include "sql/ast.h"
#include "storage/buffer_pool.h"

namespace minidb {

// A row flowing through the operator pipeline.
typedef std::vector<Value> Row;

// Volcano-style iterator. Pull rows with Open / Next / Close. Each operator
// publishes the qualified names ("table.column") of its output columns so the
// parent operator and the executor can resolve references by name.
class Operator {
 public:
  virtual ~Operator() {}
  virtual void Open() = 0;
  virtual bool Next(Row& out) = 0;
  virtual void Close() {}
  const std::vector<std::string>& OutCols() const { return out_cols_; }

 protected:
  std::vector<std::string> out_cols_;
};

// Full table scan over a heap file. Rows are materialized on Open (simple and
// sufficient for MVP data sizes); the operator interface stays pull-based.
class SeqScan : public Operator {
 public:
  SeqScan(BufferPool* bp, const TableInfo* t);
  void Open() override;
  bool Next(Row& out) override;
  long RowsRead() const { return rows_read_; }

 private:
  BufferPool*      bp_;
  const TableInfo* t_;
  std::vector<Row> rows_;
  size_t           idx_;
  long             rows_read_;
};

// Index scan over the primary-key B+Tree for keys in [low, high]. Only matching
// RIDs are fetched from the heap -- this is what makes a selective lookup cheap.
class IndexScan : public Operator {
 public:
  IndexScan(BufferPool* bp, const TableInfo* t, int32_t low, int32_t high);
  void Open() override;
  bool Next(Row& out) override;
  long RowsRead() const { return rows_read_; }

 private:
  BufferPool*      bp_;
  const TableInfo* t_;
  int32_t          low_, high_;
  std::vector<Row> rows_;
  size_t           idx_;
  long             rows_read_;
};

// Compiled predicate: column position within the child's output + op + constant.
struct CompiledPred {
  int    col_index;
  CompOp op;
  Value  value;
};

// Pass through only rows satisfying all predicates (AND).
class Filter : public Operator {
 public:
  Filter(std::unique_ptr<Operator> child, std::vector<CompiledPred> preds);
  void Open() override;
  bool Next(Row& out) override;

 private:
  std::unique_ptr<Operator> child_;
  std::vector<CompiledPred> preds_;
};

// Output only the selected columns, in the requested order.
class Project : public Operator {
 public:
  Project(std::unique_ptr<Operator> child, std::vector<int> indices,
          std::vector<std::string> out_names);
  void Open() override;
  bool Next(Row& out) override;

 private:
  std::unique_ptr<Operator> child_;
  std::vector<int>          indices_;
};

// Inner equi-join via (index) nested loops. For each outer row we build a fresh
// inner operator keyed by the join value through `inner_factory`; supplying an
// IndexScan factory yields an index-nested-loop join.
class NestedLoopJoin : public Operator {
 public:
  NestedLoopJoin(std::unique_ptr<Operator> outer, int outer_key_idx,
                 std::function<std::unique_ptr<Operator>(const Value&)> inner_factory,
                 std::vector<std::string> inner_cols);
  void Open() override;
  bool Next(Row& out) override;

 private:
  std::unique_ptr<Operator> outer_;
  int                       outer_key_idx_;
  std::function<std::unique_ptr<Operator>(const Value&)> inner_factory_;
  std::vector<std::string>  inner_cols_;
  std::unique_ptr<Operator> inner_;
  Row                       cur_outer_;
  bool                      have_outer_;
};

}  // namespace minidb
