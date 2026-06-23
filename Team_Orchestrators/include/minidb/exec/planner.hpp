#pragma once
// Cost-based physical planner. Builds the operator tree for a SELECT, choosing
// the access path (index vs sequential scan) from statistics and the join
// algorithm (hash vs nested-loop) from estimated cardinalities. explain()
// renders the chosen plan with its cost estimates.
#include "minidb/catalog.hpp"
#include "minidb/exec/operators.hpp"
#include "minidb/sql/ast.hpp"
#include "minidb/storage/storage_engine.hpp"
#include <memory>
#include <string>

namespace minidb {

class Planner {
 public:
  std::unique_ptr<Operator> build(const SelectStmt& s, Catalog& catalog, StorageEngine& engine);
  std::string explain(const SelectStmt& s, Catalog& catalog, StorageEngine& engine);

 private:
  std::unique_ptr<Operator> build_internal(const SelectStmt& s, Catalog& catalog,
                                           StorageEngine& engine, std::string* trace);
  // Row count for a table: cached stats, else counted by a scan (and cached).
  size_t row_estimate(Catalog& c, StorageEngine& e, TableId t);
  // Equality selectivity 1/distinct from stats, else a default fraction.
  double eq_selectivity(Catalog& c, TableId t, size_t col);
};

}  // namespace minidb
