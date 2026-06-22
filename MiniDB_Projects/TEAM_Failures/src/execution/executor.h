// ============================================================================
// executor.h  --  The query EXECUTION engine, built on the "Volcano" model.
//
// In the Volcano (a.k.a. iterator) model every operator implements the same
// tiny interface:  init() once, then next() repeatedly until it returns false.
// Operators are composed into a tree; calling next() on the root pulls one row
// up through the whole tree.  Example plan for
//     SELECT name FROM users JOIN orders ON users.id = orders.uid WHERE age > 30
//
//     Projection(name)
//        └── NestedLoopJoin(users.id = orders.uid)
//               ├── SeqScan(users) WHERE age > 30      <- outer
//               └── IndexScan(orders on uid)           <- inner (probed per row)
//
// This pull-based design means rows stream through one at a time without
// materializing huge intermediate results.
// ============================================================================
#pragma once

#include "catalog/catalog.h"
#include "common/common.h"
#include "record/tuple.h"
#include "sql/ast.h"

namespace minidb {

// Describes one output column of an operator: its originating table, its name,
// and its type.  The qualifier lets us resolve "users.id" vs "orders.id".
struct ColumnMeta {
  string table;
  string name;
  TypeId      type;
};

// Evaluate one predicate (col OP constant) against a row described by `cols`.
bool evalPredicate(const Predicate &p, const vector<ColumnMeta> &cols,
                   const Tuple &row);
// True iff every predicate holds (predicates are ANDed).
bool evalAll(const vector<Predicate> &ps, const vector<ColumnMeta> &cols,
             const Tuple &row);
// Resolve a (table, column) reference to its index in `cols`, or -1.
int resolveColumn(const vector<ColumnMeta> &cols, const string &table,
                  const string &name);
// Build the ColumnMeta list for a whole table (each column qualified by table).
vector<ColumnMeta> makeTableColumns(TableInfo *t);

// --- Base operator -----------------------------------------------------------
class Executor {
 public:
  virtual ~Executor() = default;
  virtual void init() = 0;
  virtual bool next(Tuple *out) = 0;
  virtual const vector<ColumnMeta> &columns() const = 0;
};

// --- Sequential scan: read every live tuple of a table, keep those matching ---
class SeqScanExecutor : public Executor {
 public:
  SeqScanExecutor(TableInfo *table, vector<Predicate> preds);
  void init() override;
  bool next(Tuple *out) override;
  const vector<ColumnMeta> &columns() const override { return cols_; }

 private:
  TableInfo               *table_;
  vector<Predicate>   preds_;
  vector<ColumnMeta>  cols_;
  unique_ptr<TableHeap::Iterator> it_;
};

// --- Index scan: use a B+ Tree to fetch only RIDs in a key range -------------
class IndexScanExecutor : public Executor {
 public:
  // low/high are the key bounds (nullptr = open).  `residual` predicates are
  // applied to each fetched tuple (e.g. extra conditions the index can't check).
  IndexScanExecutor(TableInfo *table, IndexInfo *index,
                    unique_ptr<Value> low, unique_ptr<Value> high,
                    vector<Predicate> residual);
  void init() override;
  bool next(Tuple *out) override;
  const vector<ColumnMeta> &columns() const override { return cols_; }

 private:
  TableInfo              *table_;
  IndexInfo              *index_;
  unique_ptr<Value>  low_, high_;
  vector<Predicate>  residual_;
  vector<ColumnMeta> cols_;
  vector<RID>        rids_;
  size_t                  pos_{0};
};

// --- Nested-loop join: for each outer row, find matching inner rows ----------
// If the inner table has an index on the join column we probe it (index nested
// loop join); otherwise we scan the inner heap for each outer row.
class NestedLoopJoinExecutor : public Executor {
 public:
  NestedLoopJoinExecutor(unique_ptr<Executor> outer, TableInfo *inner,
                         IndexInfo *inner_index,
                         string outer_table, string outer_col,
                         string inner_col,
                         vector<Predicate> inner_preds);
  void init() override;
  bool next(Tuple *out) override;
  const vector<ColumnMeta> &columns() const override { return cols_; }

 private:
  void loadInnerMatches(const Tuple &outer_row);  // fill inner_rows_ for one outer row

  unique_ptr<Executor> outer_;
  TableInfo                *inner_;
  IndexInfo                *inner_index_;
  string               outer_table_, outer_col_, inner_col_;
  vector<Predicate>    inner_preds_;
  vector<ColumnMeta>   cols_;       // outer cols ++ inner cols
  vector<ColumnMeta>   inner_cols_;
  int                       outer_join_idx_{-1};

  Tuple                     cur_outer_;
  vector<Tuple>        inner_rows_;
  size_t                    inner_pos_{0};
};

// --- Projection: output only selected columns (and reorder them) -------------
class ProjectionExecutor : public Executor {
 public:
  ProjectionExecutor(unique_ptr<Executor> child, vector<int> col_indexes,
                     vector<ColumnMeta> out_cols);
  void init() override;
  bool next(Tuple *out) override;
  const vector<ColumnMeta> &columns() const override { return cols_; }

 private:
  unique_ptr<Executor> child_;
  vector<int>          idxs_;
  vector<ColumnMeta>   cols_;
};

}  // namespace minidb
