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
#include "buffer_pool.h"
#include "heap_file.h"
#include "lock_manager.h"

namespace minidb {

class Transaction;  // transaction.h — undo log for rollback (Phase D)

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

// ---- Tuple serialization (Phase A2) ----
//
// Wire format per value: 1-byte type tag, then int64 for Int, or uint32 length
// + raw bytes for Text. This is the only on-page record format.
std::vector<char> serializeTuple(const Tuple& t);
Tuple deserializeTuple(const char* data, size_t len, const Schema& schema);

// ---- Storage (Phase A3: page-backed heap table) ----
//
// Rows live in slotted pages backed by a BufferPool over a heap file
// ("<name>.dat"); the primary-key B+ Tree, when present, is backed by its own
// pages ("<name>.idx"). A RID is (page_id, slot_id). `fresh` truncates the
// backing files for a new table; pass false to reopen durable state on restart.
class Table {
 public:
  Table(std::string name, Schema schema, int pk_index, bool fresh = true);

  const std::string& name() const { return name_; }
  const Schema& schema() const { return schema_; }
  int pk_index() const { return pk_index_; }  // -1 if no primary key
  bool has_pk() const { return pk_index_ >= 0; }
  bool hasIntPk() const {
    return has_pk() && schema_[pk_index_].type == ValueType::Int;
  }

  // Append a serialized row to the last page (or a fresh page); updates the PK
  // index. Returns its RID.
  RID insert(const Tuple& t);

  // Tombstone a row (slot length 0) and remove it from the PK index.
  void markDeleted(RID rid);

  // Read a row. Returns false if the slot is out of range or tombstoned;
  // otherwise deserializes it into `out`.
  bool readRecord(RID rid, Tuple& out) const;

  // For undo: the stored byte length of a slot, and restoring it after delete.
  uint16_t slotLength(RID rid) const;
  void restoreSlot(RID rid, uint16_t len);

  int numPages() const { return num_pages_; }
  int slotsInPage(int page_id) const;
  size_t size() const { return num_records_; }  // physical rows (incl. tombstones)

  void flush();  // checkpoint this table's data + index pages to disk

  const BPlusTree& index() const { return pk_tree_; }
  BPlusTree& index() { return pk_tree_; }

 private:
  std::string name_;
  Schema schema_;
  int pk_index_;

  std::unique_ptr<HeapFile> data_heap_;
  std::unique_ptr<BufferPool> data_pool_;
  int num_pages_ = 0;
  int last_page_ = -1;
  size_t num_records_ = 0;

  std::unique_ptr<HeapFile> idx_heap_;  // only when hasIntPk()
  std::unique_ptr<BufferPool> idx_pool_;
  BPlusTree pk_tree_;
};

class Catalog {
 public:
  Table* createTable(const std::string& name, Schema schema, int pk_index,
                     bool fresh = true);
  Table* getTable(const std::string& name);

  // Flush every table's pages to disk (used by the checkpoint daemon).
  void checkpointAll();

 private:
  std::unordered_map<std::string, std::unique_ptr<Table>> tables_;
};

// ---- Execution context (transaction + locking) ----

// Threaded through operators so scans/DML take the right table lock under
// Strict 2PL. If lock_mgr is null, locking is skipped (handy for unit tests).
// When `txn_obj` is set, DML operators record undo actions on it so an abort
// can roll back (Phase D).
struct ExecContext {
  LockManager* lock_mgr = nullptr;
  TxnId txn = 0;
  Transaction* txn_obj = nullptr;

  void lock(const std::string& table, LockMode mode) const {
    if (lock_mgr) lock_mgr->acquire(txn, table, mode);
  }
};

// ---- Predicates ----

enum class CompareOp { Eq, Ne, Lt, Le, Gt, Ge };

// Simple "column <op> constant" comparison — the leaf of a WHERE expression.
// `column` is an index into the tuple/schema (resolved by the optimizer).
struct Predicate {
  int column = 0;
  CompareOp op = CompareOp::Eq;
  Value constant;

  bool eval(const Tuple& t) const;
};

// A boolean WHERE expression tree: comparison leaves combined with AND/OR.
// The optimizer builds this (with columns resolved to indices) and hands it to
// a Filter, which evaluates it per tuple with short-circuit semantics.
struct PredExpr {
  enum class Kind { Leaf, And, Or };
  Kind kind = Kind::Leaf;
  Predicate leaf;                          // valid when kind == Leaf
  std::shared_ptr<PredExpr> left, right;   // valid when kind == And/Or

  bool eval(const Tuple& t) const;

  static std::shared_ptr<PredExpr> makeLeaf(const Predicate& p) {
    auto e = std::make_shared<PredExpr>();
    e->kind = Kind::Leaf;
    e->leaf = p;
    return e;
  }
  static std::shared_ptr<PredExpr> makeAnd(std::shared_ptr<PredExpr> a,
                                           std::shared_ptr<PredExpr> b) {
    auto e = std::make_shared<PredExpr>();
    e->kind = Kind::And;
    e->left = std::move(a);
    e->right = std::move(b);
    return e;
  }
  static std::shared_ptr<PredExpr> makeOr(std::shared_ptr<PredExpr> a,
                                          std::shared_ptr<PredExpr> b) {
    auto e = std::make_shared<PredExpr>();
    e->kind = Kind::Or;
    e->left = std::move(a);
    e->right = std::move(b);
    return e;
  }
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
// Walks page 0..N, and within each page walks its live slots.
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
  int page_ = 0;
  int slot_ = 0;
  int num_pages_ = 0;
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

// Applies a WHERE predicate to its child stream. Accepts either a single
// comparison (wrapped as a leaf) or a full AND/OR expression tree.
class Filter : public Operator {
 public:
  Filter(OperatorPtr child, Predicate pred)
      : child_(std::move(child)), expr_(PredExpr::makeLeaf(pred)) {}
  Filter(OperatorPtr child, std::shared_ptr<PredExpr> expr)
      : child_(std::move(child)), expr_(std::move(expr)) {}
  void open() override { child_->open(); }
  bool next(Tuple& out) override;
  void close() override { child_->close(); }
  const Schema& schema() const override { return child_->schema(); }

 private:
  OperatorPtr child_;
  std::shared_ptr<PredExpr> expr_;
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
