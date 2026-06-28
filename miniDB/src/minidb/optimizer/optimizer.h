#pragma once

#include <string>
#include <vector>

#include "minidb/sql/parser.h"

namespace minidb {

class ExecutionEngine;

class Optimizer {
 public:
  // Estimate selectivity of a predicate (0.0 to 1.0)
  static double EstimateSelectivity(const Predicate& predicate, bool is_primary_key);

  // Decide whether to use index scan or table scan
  static bool ShouldUseIndexScan(const Predicate& predicate, bool is_primary_key);

  // Choose optimal join order (returns true if table1 should be outer, false if table2 should be outer)
  static bool ChooseJoinOrder(std::size_t table1_size, std::size_t table2_size);
};

}  // namespace minidb
