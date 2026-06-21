#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "catalog/table.h"
#include "exec/expression.h"
#include "storage/heap_file.h"

namespace walterdb {

// ===========================================================================
// Volcano-model physical operators.  Each implements open()/next()/close():
// next() returns the next output Row or nullopt at end-of-stream, pulling from
// its children on demand.  This demand-driven pipeline is the classic iterator
// model -- one row flows through the whole tree per next() at the root.
//
// describe() + children() let the planner render an EXPLAIN tree.  The
// human-readable description (including the optimizer's cost estimate) is baked
// in by the planner when it constructs each operator.
// ===========================================================================

class Operator {
 public:
  virtual ~Operator() = default;
  virtual void open() = 0;
  virtual std::optional<Row> next() = 0;
  virtual void close() = 0;
  virtual const ResultSchema& schema() const = 0;

  virtual std::string describe() const = 0;
  virtual std::vector<const Operator*> children() const { return {}; }
};
using OperatorPtr = std::unique_ptr<Operator>;

// Full-table scan: walks the heap file's page chain (the "table scan" path).
class SeqScanOp : public Operator {
 public:
  SeqScanOp(Table* table, std::string qualifier, std::string desc);
  void open() override;
  std::optional<Row> next() override;
  void close() override;
  const ResultSchema& schema() const override { return schema_; }
  std::string describe() const override { return desc_; }

 private:
  Table* table_;
  ResultSchema schema_;
  std::string desc_;
  std::optional<HeapFile::Cursor> cursor_;
};

// Primary-key point lookup via the B+tree (the "index scan" path): at most one
// row.  Chosen by the optimizer when WHERE has an equality on the PK column.
class IndexScanOp : public Operator {
 public:
  IndexScanOp(Table* table, std::string qualifier, Value key, std::string desc);
  void open() override;
  std::optional<Row> next() override;
  void close() override;
  const ResultSchema& schema() const override { return schema_; }
  std::string describe() const override { return desc_; }

 private:
  Table* table_;
  ResultSchema schema_;
  Value key_;
  std::string desc_;
  bool done_ = false;
};

// Keeps only rows for which the predicate evaluates to TRUE.
class FilterOp : public Operator {
 public:
  FilterOp(OperatorPtr child, const Expr* predicate, std::string desc);
  void open() override;
  std::optional<Row> next() override;
  void close() override;
  const ResultSchema& schema() const override { return child_->schema(); }
  std::string describe() const override { return desc_; }
  std::vector<const Operator*> children() const override { return {child_.get()}; }

 private:
  OperatorPtr child_;
  const Expr* predicate_;
  std::string desc_;
};

// Computes the output column list (the SELECT list), expanding `*` / `t.*`.
class ProjectionOp : public Operator {
 public:
  struct Item {
    const Expr* expr = nullptr;  // evaluate this, OR ...
    int passthrough = -1;        // ... copy this child column index (for star)
  };
  ProjectionOp(OperatorPtr child, std::vector<Item> items, ResultSchema schema, std::string desc);
  void open() override;
  std::optional<Row> next() override;
  void close() override;
  const ResultSchema& schema() const override { return schema_; }
  std::string describe() const override { return desc_; }
  std::vector<const Operator*> children() const override { return {child_.get()}; }

 private:
  OperatorPtr child_;
  std::vector<Item> items_;
  ResultSchema schema_;
  std::string desc_;
};

// Block nested-loop join: materialises the right input once, then for each left
// row emits the concatenation with every matching right row.
class NestedLoopJoinOp : public Operator {
 public:
  NestedLoopJoinOp(OperatorPtr left, OperatorPtr right, const Expr* on, std::string desc);
  void open() override;
  std::optional<Row> next() override;
  void close() override;
  const ResultSchema& schema() const override { return schema_; }
  std::string describe() const override { return desc_; }
  std::vector<const Operator*> children() const override { return {left_.get(), right_.get()}; }

 private:
  OperatorPtr left_;
  OperatorPtr right_;
  const Expr* on_;
  ResultSchema schema_;
  std::string desc_;
  std::vector<Row> right_rows_;
  size_t rj_ = 0;
  std::optional<Row> left_cur_;
};

// Render an operator tree as an indented EXPLAIN listing.
std::string explain_tree(const Operator* op);

}  // namespace walterdb
