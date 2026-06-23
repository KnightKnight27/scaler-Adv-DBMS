#pragma once
#include <memory>
#include <string>
#include "exec/executor.h"
#include "sql/ast.h"
#include "storage/row_store.h"

namespace minidb {

// Resolves table names to their open RowStore. Implemented by the engine; lets
// the optimizer plan without depending on the whole Database.
class PlanContext {
 public:
  virtual ~PlanContext() = default;
  virtual RowStore *GetStore(const std::string &table) = 0;  // throws if missing
};

// A built plan plus a human-readable EXPLAIN describing the chosen access paths
// and join order (and why).
struct PlanResult {
  std::unique_ptr<Executor> root;
  std::string               explain;
};

// A small cost-based optimizer. It estimates selectivity from per-table
// statistics (row count, observed key range), chooses between a full table scan
// and a primary-key index scan, and picks the nested-loop join order.
class Optimizer {
 public:
  explicit Optimizer(PlanContext *ctx) : ctx_(ctx) {}
  PlanResult PlanSelect(const SelectStmt &stmt);

 private:
  // Build the access path for one table given its WHERE predicates: an index
  // range scan when the cost model says a PK predicate is selective enough,
  // otherwise a sequential scan. Appends reasoning to `explain`.
  std::unique_ptr<Executor> BuildScan(const std::string &table,
                                      const std::vector<Predicate> &preds,
                                      std::string *explain);

  PlanContext *ctx_;
};

}  // namespace minidb
