// execution.h — Track 3 (Query & Concurrency)
//
// Volcano / iterator model query executor. Every operator implements
// open() / next(Tuple&) / close(); next() returns false when the stream is
// exhausted. Operators pull tuples from their children one at a time, so an
// arbitrary tree of operators streams without materializing intermediates.
//
// Operators provided:
//   TableScan       - sequential scan over a table, skipping is_deleted rows
//   IndexScan       - range scan driven by the primary-key B+ Tree
//   Filter          - applies a WHERE predicate
//   Projection      - selects a subset of columns (SELECT list)
//   NestedLoopJoin  - left-deep equi-join of two child streams
//   Insert / Delete - DML over a table (Delete sets the is_deleted flag)
//
// Storage is accessed through the abstract `Table`/`Catalog` interface below.
// This file ships an in-memory implementation so the executor runs and is
// testable today; the heap-file track can later provide a page-backed Table
// implementing the same contract without touching operator code.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "btree.h"
#include "lock_manager.h"

namespace minidb {

// ---- Value model ----

enum class ValueType { Int, Text };

struct Value {
  ValueType type = ValueType::Int;
  int64_t i = 0;
  std::string s;

  static Value Int(int64_t v) { return {ValueType::Int, v, ""}; }
  static Value Text(std::string v) { return {ValueType::Text, 0, std::move(v)}; }

  bool operator==(const Value& o) const;
  bool operator<(const Value& o) const;
  std::string toString() const;
};

using Tuple = std::vector<Value>;

struct Column {
  std::string name;
  ValueType type;
};
using Schema = std::vector<Column>;

// ---- Storage ----

struct Record {
  Tuple tuple;
  bool is_deleted = false;  // Track 3: deletes flip this flag, no compaction
};

// A table. The in-memory implementation stores rows in a vector (page_id =
// row index) and maintains a primary-key B+ Tree when a PK column exists.
class Table {
 public:
  Table(std::string name, Schema schema, int pk_index)
      : name_(std::move(name)), schema_(std::move(schema)), pk_index_(pk_index) {}

  const std::string& name() const { return name_; }
  const Schema& schema() const { return schema_; }
  int pk_index() const { return pk_index_; }  // -1 if no primary key
  bool has_pk() const { return pk_index_ >= 0; }

  // Append a row; updates the PK index. Returns its RID.
  RID insert(const Tuple& t);

  // Mark a row deleted (tombstone in both the table and the PK index).
  void markDeleted(RID rid);

  Record& record(RID rid) { return records_[rid.page_id]; }
  const Record& record(RID rid) const { return records_[rid.page_id]; }
  size_t size() const { return records_.size(); }

  const BPlusTree& index() const { return pk_tree_; }
  BPlusTree& index() { return pk_tree_; }

 private:
  std::string name_;
  Schema schema_;
  int pk_index_;
  std::vector<Record> records_;
  BPlusTree pk_tree_;
};

class Catalog {
 public:
  Table* createTable(const std::string& name, Schema schema, int pk_index);
  Table* getTable(const std::string& name);

 private:
  std::unordered_map<std::string, std::unique_ptr<Table>> tables_;
};

// ---- Execution context (transaction + locking) ----

// Threaded through operators so scans/DML take the right table lock under
// Strict 2PL. If lock_mgr is null, locking is skipped (handy for unit tests).
struct ExecContext {
  LockManager* lock_mgr = nullptr;
  TxnId txn = 0;

  void lock(const std::string& table, LockMode mode) const {
    if (lock_mgr) lock_mgr->acquire(txn, table, mode);
  }
};

// ---- Predicates ----

enum class CompareOp { Eq, Ne, Lt, Le, Gt, Ge };

// Simple "column <op> constant" predicate for WHERE clauses.
struct Predicate {
  int column = 0;
  CompareOp op = CompareOp::Eq;
  Value constant;

  bool eval(const Tuple& t) const;
};

// ---- Volcano operators ----

class Operator {
 public:
  virtual ~Operator() = default;
  virtual void open() = 0;
  virtual bool next(Tuple& out) = 0;
  virtual void close() = 0;
  virtual const Schema& schema() const = 0;
};

using OperatorPtr = std::unique_ptr<Operator>;

// Sequential scan over every live row of a table (acquires a Shared lock).
class TableScan : public Operator {
 public:
  TableScan(Table* table, const ExecContext& ctx) : table_(table), ctx_(ctx) {}
  void open() override;
  bool next(Tuple& out) override;
  void close() override {}
  const Schema& schema() const override { return table_->schema(); }

 private:
  Table* table_;
  ExecContext ctx_;
  size_t cursor_ = 0;
};

// Range scan over the primary-key B+ Tree for [low, high] (acquires Shared).
// Used by the optimizer when a WHERE clause targets the primary key.
class IndexScan : public Operator {
 public:
  IndexScan(Table* table, Key low, Key high, const ExecContext& ctx)
      : table_(table), low_(low), high_(high), ctx_(ctx) {}
  void open() override;
  bool next(Tuple& out) override;
  void close() override {}
  const Schema& schema() const override { return table_->schema(); }

 private:
  Table* table_;
  Key low_, high_;
  ExecContext ctx_;
  BPlusTree::Iterator it_;
};

// Applies a WHERE predicate to its child stream.
class Filter : public Operator {
 public:
  Filter(OperatorPtr child, Predicate pred)
      : child_(std::move(child)), pred_(pred) {}
  void open() override { child_->open(); }
  bool next(Tuple& out) override;
  void close() override { child_->close(); }
  const Schema& schema() const override { return child_->schema(); }

 private:
  OperatorPtr child_;
  Predicate pred_;
};

// Projects a subset of columns (the SELECT list).
class Projection : public Operator {
 public:
  Projection(OperatorPtr child, std::vector<int> cols);
  void open() override { child_->open(); }
  bool next(Tuple& out) override;
  void close() override { child_->close(); }
  const Schema& schema() const override { return out_schema_; }

 private:
  OperatorPtr child_;
  std::vector<int> cols_;
  Schema out_schema_;
};

// Left-deep nested-loop equi-join: out = outer ⨝ inner on outer[lc]==inner[rc].
// For each outer tuple the inner child is re-opened and scanned in full.
class NestedLoopJoin : public Operator {
 public:
  NestedLoopJoin(OperatorPtr outer, OperatorPtr inner, int left_col,
                 int right_col);
  void open() override;
  bool next(Tuple& out) override;
  void close() override;
  const Schema& schema() const override { return out_schema_; }

 private:
  OperatorPtr outer_, inner_;
  int left_col_, right_col_;
  Schema out_schema_;
  Tuple outer_tuple_;
  bool have_outer_ = false;
};

// INSERT: pushes one supplied tuple into the table (acquires Exclusive).
class Insert : public Operator {
 public:
  Insert(Table* table, Tuple row, const ExecContext& ctx)
      : table_(table), row_(std::move(row)), ctx_(ctx) {}
  void open() override;
  bool next(Tuple& out) override;  // emits nothing; returns false
  void close() override {}
  const Schema& schema() const override { return table_->schema(); }
  int inserted() const { return inserted_; }

 private:
  Table* table_;
  Tuple row_;
  ExecContext ctx_;
  bool done_ = false;
  int inserted_ = 0;
};

// DELETE ... WHERE: scans via `child`, tombstones matching rows (Exclusive).
// `child` must emit rows of `table` (its first column index is the RID source
// is resolved by re-finding the PK), so we delete by primary key.
class Delete : public Operator {
 public:
  Delete(Table* table, OperatorPtr child, const ExecContext& ctx)
      : table_(table), child_(std::move(child)), ctx_(ctx) {}
  void open() override;
  bool next(Tuple& out) override;  // emits nothing; returns false
  void close() override { child_->close(); }
  const Schema& schema() const override { return table_->schema(); }
  int deleted() const { return deleted_; }

 private:
  Table* table_;
  OperatorPtr child_;
  ExecContext ctx_;
  bool done_ = false;
  int deleted_ = 0;
};

// Drains an operator tree to completion, collecting all output tuples.
std::vector<Tuple> execute(Operator& root);

}  // namespace minidb
