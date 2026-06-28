#include "minidb/optimizer/optimizer.h"

namespace minidb {

double Optimizer::EstimateSelectivity(const Predicate& predicate, bool is_primary_key) {
  if (predicate.empty) {
    return 1.0;
  }
  
  // Very basic heuristic based selectivity estimation
  if (predicate.op == "=") {
    return is_primary_key ? 0.01 : 0.1;
  }
  if (predicate.op == "!=") {
    return is_primary_key ? 0.99 : 0.9;
  }
  if (predicate.op == ">=" || predicate.op == "<=") {
    return 0.5;
  }
  
  return 0.2; // default
}

bool Optimizer::ShouldUseIndexScan(const Predicate& predicate, bool is_primary_key) {
  if (predicate.empty) return false;
  // If it's a primary key equality predicate, we should definitely use the index
  if (is_primary_key && predicate.op == "=") {
    return true;
  }
  return false;
}

bool Optimizer::ChooseJoinOrder(std::size_t table1_size, std::size_t table2_size) {
  // Simple cost-based join ordering: smaller table should be the outer loop in nested loop join
  return table1_size <= table2_size;
}

}  // namespace minidb
