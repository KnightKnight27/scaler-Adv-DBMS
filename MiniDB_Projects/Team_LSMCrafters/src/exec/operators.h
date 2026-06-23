#pragma once
#include <memory>
#include <string>
#include <vector>
#include "common/types.h"
#include "sql/ast.h"
#include "storage/storage_engine.h"

namespace minidb {

// Volcano-style physical operator: open(), pull rows with next(), then close().
// Each operator exposes the schema of the rows it produces.
class PhysicalOperator {
 public:
  virtual void open()                 = 0;
  virtual bool next(Row& out)         = 0;
  virtual void close()                = 0;
  virtual const Schema& out_schema() const = 0;

  // For EXPLAIN: a one-line label and this node's child operators.
  virtual std::string describe() const = 0;
  virtual std::vector<const PhysicalOperator*> children() const { return {}; }

  virtual ~PhysicalOperator() = default;
};
using OperatorPtr = std::unique_ptr<PhysicalOperator>;

// Renders an operator tree as indented text (used by EXPLAIN).
std::string explain_plan(const PhysicalOperator& root);

// Reads and deserializes every row from a storage engine.
class SeqScan : public PhysicalOperator {
 public:
  SeqScan(StorageEngine& store, Schema schema, std::string table);
  void open() override;
  bool next(Row& out) override;
  void close() override;
  const Schema& out_schema() const override { return schema_; }
  std::string describe() const override { return "SeqScan(" + table_ + ")"; }

 private:
  StorageEngine&             store_;
  Schema                     schema_;
  std::string                table_;
  std::unique_ptr<RowCursor> cursor_;
};

// Reads only rows whose primary key falls in [lo, hi], using the B+Tree index.
class IndexScan : public PhysicalOperator {
 public:
  IndexScan(StorageEngine& store, Schema schema, std::string table, Key lo, Key hi);
  void open() override;
  bool next(Row& out) override;
  void close() override;
  const Schema& out_schema() const override { return schema_; }
  std::string describe() const override;

 private:
  StorageEngine&             store_;
  Schema                     schema_;
  std::string                table_;
  Key                        lo_, hi_;
  std::unique_ptr<RowCursor> cursor_;
};

// Passes through only the rows for which the predicate holds.
class Filter : public PhysicalOperator {
 public:
  Filter(OperatorPtr child, const Expr* predicate);
  void open() override { child_->open(); }
  bool next(Row& out) override;
  void close() override { child_->close(); }
  const Schema& out_schema() const override { return child_->out_schema(); }
  std::string describe() const override { return "Filter"; }
  std::vector<const PhysicalOperator*> children() const override { return {child_.get()}; }

 private:
  OperatorPtr child_;
  const Expr* predicate_;
};

// Joins two child operators: for every outer row, scan the inner side and emit
// pairs that satisfy the ON predicate (concatenated outer+inner row).
class NestedLoopJoin : public PhysicalOperator {
 public:
  NestedLoopJoin(OperatorPtr outer, OperatorPtr inner, const Expr* on);
  void open() override;
  bool next(Row& out) override;
  void close() override;
  const Schema& out_schema() const override { return schema_; }
  std::string describe() const override { return "NestedLoopJoin"; }
  std::vector<const PhysicalOperator*> children() const override {
    return {outer_.get(), inner_.get()};
  }

 private:
  OperatorPtr outer_, inner_;
  const Expr* on_;
  Schema      schema_;
  Row         outer_row_;
  bool        have_outer_ = false;
};

// Keeps only the selected columns of each row.
class Project : public PhysicalOperator {
 public:
  Project(OperatorPtr child, const std::vector<ColumnRef>& columns);
  void open() override { child_->open(); }
  bool next(Row& out) override;
  void close() override { child_->close(); }
  const Schema& out_schema() const override { return schema_; }
  std::string describe() const override { return "Project"; }
  std::vector<const PhysicalOperator*> children() const override { return {child_.get()}; }

 private:
  OperatorPtr      child_;
  std::vector<int> indices_;
  Schema           schema_;
};

}  // namespace minidb
