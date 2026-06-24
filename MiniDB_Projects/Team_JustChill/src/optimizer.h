// optimizer.h — Track 3 (Query & Concurrency): cost-based planner
//
// A deliberately small cost-based optimizer. It consumes the parser's AST
// (parser.h) plus the Catalog and produces a physical operator tree
// (execution.h) ready to run, making two classic optimizer decisions:
//
//   1. Access-path selection. For a single-table predicate it costs a
//      sequential TableScan (O(N)) against a primary-key IndexScan
//      (O(log N) descent + matched rows) and picks the cheaper plan. A WHERE
//      on the integer primary key with =, <, <=, >, >= can use the B+ Tree;
//      anything else falls back to a full scan.
//
//   2. Join ordering. For a two-table equi-join the smaller estimated relation
//      is chosen as the outer (driving) side of the NestedLoopJoin, since the
//      inner side is re-scanned once per outer row.
//
// Selectivity is estimated structurally (no histograms): an equality on the
// unique PK is ~1 row (1/N); a range is ~1/3 of the table; an unbounded scan
// is the whole table. The model is intentionally coarse but enough to drive
// the index-vs-scan and join-order choices, and every decision is reported in
// the human-readable `explain` string for `EXPLAIN ...` queries and the viva.
#pragma once

#include <string>

#include "execution.h"
#include "parser.h"

namespace minidb {

// The output of planning: a runnable operator tree, the estimated cost of the
// chosen plan, a multi-line EXPLAIN rendering, and whether the root is DML
// (INSERT/DELETE) so the caller can report affected-row counts instead of a
// result set.
struct PhysicalPlan {
  OperatorPtr root;
  double cost = 0.0;
  std::string explain;
  bool is_dml = false;
};

class Optimizer {
 public:
  explicit Optimizer(Catalog& catalog) : catalog_(catalog) {}

  // Plan a parsed statement against the catalog. `ctx` is threaded into the
  // produced operators for lock acquisition. Throws std::runtime_error for
  // semantic errors (unknown table/column, arity/type mismatch).
  PhysicalPlan optimize(const Statement& stmt, const ExecContext& ctx);

 private:
  Catalog& catalog_;
};

}  // namespace minidb
