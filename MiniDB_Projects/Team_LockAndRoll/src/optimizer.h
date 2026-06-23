#pragma once

#include <memory>
#include <string>
#include <vector>

#include "catalog.h"
#include "parser.h"

namespace minidb {

enum class PlanType { SeqScan, IndexScan, Filter, NestedLoopJoin, Projection, Aggregation };

struct PlanNode {
  PlanType type;

  TableInfo* table = nullptr;
  std::string alias;
  bool index_point = false;
  int64_t index_key = 0;

  ExprPtr predicate;
  ExprPtr join_on;

  std::vector<SelectItem> items;
  std::vector<std::string> group_by;

  std::vector<std::shared_ptr<PlanNode>> children;

  double est_rows = 0;
  double est_cost = 0;
};
using PlanPtr = std::shared_ptr<PlanNode>;

class Optimizer {
 public:
  explicit Optimizer(Catalog* catalog) : catalog_(catalog) {}

  PlanPtr build(const SelectStmt& stmt);

  static std::string explain(const PlanPtr& plan);

 private:
  double table_rows(TableInfo* t) const;
  double selectivity(const ExprPtr& pred, TableInfo* t, const std::string& alias) const;
  bool pk_equality(const ExprPtr& where, TableInfo* t, const std::string& alias,
                   int64_t* key_out) const;

  Catalog* catalog_;
};

}  // namespace minidb
