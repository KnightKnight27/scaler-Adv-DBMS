#pragma once

#include <cstddef>
#include <string>

#include "minidb/query/parser.h"

namespace minidb {

enum class AccessPath {
  SequentialScan,
  IndexScan,
  DirectInsert,
  NestedLoopJoin,
  IndexNestedLoopJoin
};

struct TableStats {
  std::size_t row_count{};
  std::size_t page_count{};
  std::size_t tree_height{1};
};

struct QueryPlan {
  Query query;
  AccessPath access_path;
  double sequential_cost{};
  double index_cost{};
  std::string explanation;
  std::string outer_table;
  std::string inner_table;
};

class Optimizer {
 public:
  QueryPlan Optimize(const Query &query, std::size_t page_count,
                     std::size_t row_count, std::size_t tree_height,
                     bool has_index = true) const;
  QueryPlan OptimizeJoin(const Query &query, const TableStats &left,
                         const TableStats &right, bool left_has_index = true,
                         bool right_has_index = true) const;
};

}  // namespace minidb
