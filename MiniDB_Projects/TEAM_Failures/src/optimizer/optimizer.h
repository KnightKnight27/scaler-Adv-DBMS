// ============================================================================
// optimizer.h  --  A small COST-BASED optimizer.
//
// The same query can run many ways; the optimizer estimates the cost of the
// alternatives and picks the cheapest.  MiniDB makes two decisions:
//
//   1. ACCESS PATH: for each table, scan the whole heap (SeqScan) or use a B+
//      Tree index to fetch only matching rows (IndexScan)?  We estimate how many
//      rows a predicate keeps (its SELECTIVITY) and compare costs:
//         seq cost   ≈ number of tuples in the table
//         index cost ≈ number of tuples the index is estimated to return
//      An index wins only when it is selective enough.
//
//   2. JOIN ORDER: for a two-table join, which table drives the outer loop?  We
//      put the table that yields FEWER rows on the outside, because a nested
//      loop runs the inner side once per outer row.
//
// selectivity is estimated with classic textbook heuristics (we keep only a row
// count per table, not full histograms):
//      equality on a unique/PK column : 1 row
//      equality on other columns      : 10% of rows
//      a range (<, >, <=, >=)         : 33% of rows
// ============================================================================
#pragma once

#include "common/common.h"
#include "catalog/catalog.h"
#include "sql/ast.h"
#include "record/value.h"

namespace minidb {

// The chosen way to read one table.
struct ScanChoice {
  bool                    use_index{false};
  IndexInfo              *index{nullptr};
  unique_ptr<Value>  low;            // index lower bound (null = open)
  unique_ptr<Value>  high;           // index upper bound (null = open)
  vector<Predicate>  residual;       // predicates to re-check after fetch
  double                  est_rows{0};     // estimated rows produced
  double                  est_cost{0};     // estimated cost (tuples touched)
  string             explain;         // human-readable, shown by EXPLAIN
};

class Optimizer {
 public:
  // Choose SeqScan vs IndexScan for `table` given the predicates on it.
  static ScanChoice chooseScan(TableInfo *table, const vector<Predicate> &preds);

  // For a join, return true if `a` should be the OUTER (driving) table given the
  // local predicates on each side.
  static bool aShouldBeOuter(TableInfo *a, const vector<Predicate> &a_preds,
                             TableInfo *b, const vector<Predicate> &b_preds);

  // Estimate the fraction of rows a single predicate keeps (0..1).
  static double selectivity(TableInfo *table, const Predicate &p);
};

}  // namespace minidb
