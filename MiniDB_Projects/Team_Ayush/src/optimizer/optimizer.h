#pragma once
#include <string>
#include <vector>
#include "catalog/catalog.h"
#include "sql/ast.h"

namespace minidb {

// The result of choosing how to access a single table.
struct AccessChoice {
  bool    use_index = false;   // index scan vs sequential scan
  int32_t low = 0;             // inclusive key bounds (valid if use_index)
  int32_t high = 0;
  double  est_rows = 0;        // estimated rows produced
  double  seq_cost = 0;        // estimated cost of a full scan
  double  index_cost = 0;      // estimated cost of the index scan (if applicable)
  std::string reason;          // human-readable justification for EXPLAIN
};

// A deliberately simple, textbook cost model: selectivity heuristics drive
// estimated cardinalities, and the access-path cost is (roughly) the number of
// rows touched. It is enough to make sound table-scan vs index-scan decisions
// and to pick a join order.
class Optimizer {
 public:
  // Estimated fraction of rows of `t` passing predicate `p`.
  static double Selectivity(const TableInfo& t, const Predicate& p);

  // Choose seq-scan vs index-scan for table `t` given predicates that have
  // already been determined to apply to it.
  static AccessChoice ChooseAccess(const TableInfo& t,
                                   const std::vector<Predicate>& preds);
};

}  // namespace minidb
