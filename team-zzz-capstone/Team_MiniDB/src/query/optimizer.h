#pragma once
#include <memory>
#include <string>

#include "../catalog/catalog.h"
#include "../sql/ast.h"
#include "plan_node.h"

namespace minidb {

// Cost-based optimizer covering the two decisions the rubric asks for:
//   1. table scan vs. index scan for a base relation, given equality
//      predicates on the primary key;
//   2. join order (which side drives the nested-loop) and join algorithm
//      (index nested-loop probe vs. plain nested-loop), based on estimated
//      cardinalities after applying known-selectivity filters.
//
// Cost units are abstract "page touches": SeqScan ~ N rows, IndexScan ~
// log2(N)+1, NestedLoopJoin ~ outer_card * inner_cost. There's no
// histogram - selectivity for non-PK predicates is a fixed constant
// (documented in README's Optimizer section as a simplifying assumption).
class Optimizer {
 public:
  explicit Optimizer(Catalog *catalog) : catalog_(catalog) {}

  // Builds an executable plan for a SELECT, and fills `explain` with a
  // human-readable trace of the access paths and join strategy chosen.
  std::unique_ptr<PlanNode> PlanSelect(const SelectStmt &stmt, std::string *explain);

  // Shared with DELETE ... WHERE, which also benefits from index-driven
  // row identification instead of a full scan.
  std::unique_ptr<PlanNode> PlanBaseRelation(TableInfo *table, const std::string &alias,
                                              const std::vector<Comparison> &applicable_preds,
                                              std::vector<bool> *consumed, std::string *explain);

 private:
  Catalog *catalog_;
};

}  // namespace minidb
