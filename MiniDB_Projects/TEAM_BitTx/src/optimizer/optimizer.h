#pragma once

#include "catalog/catalog.h"
#include "execution/executor.h"
#include "optimizer/stats.h"
#include "parser/ast.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace minidb {

// Cost-based optimizer. Walks an executor tree, estimates per-node cost,
// and rewrites it to use cheaper operators when alternatives exist.
//
// Currently implemented rules:
// 1. Replace SeqScan with IndexScan when an equality predicate is on the
// primary key column and a B+ tree index is available.
// 2. Push a top-level Filter below a Project / Sort / Limit when it is safe
// and reduces rows.
// 3. Reorder NestedLoopJoin so the smaller-table scan drives the outer loop.
class Optimizer {
public:
  Optimizer(CatalogManager* catalog, unordered_map<string, TableStats> stats)
      : catalog_(catalog), stats_(move(stats)) {}

  // Rewrite an executor plan in place. Returns the optimized root.
  unique_ptr<AbstractExecutor> Optimize(unique_ptr<AbstractExecutor> root);

  // Cost estimate per node, in abstract "row-touches" units.
  struct Cost {
    double rows = 0.0; // estimated output rows
    double cpu = 0.0;  // estimated CPU work units
  };
  Cost Estimate(AbstractExecutor* node) const;

  const unordered_map<string, TableStats>& GetStats() const {
    return stats_;
  }

private:
  // Helper: try to swap a SeqScan for an IndexScan given the filter predicate.
  unique_ptr<AbstractExecutor> TryIndexScan(SeqScanExecutor* scan,
                                            FilterExecutor* filter) const;

  CatalogManager* catalog_;
  unordered_map<string, TableStats> stats_;
};

} // namespace minidb