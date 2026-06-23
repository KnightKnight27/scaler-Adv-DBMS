#include "minidb/query/optimizer.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace minidb {

QueryPlan Optimizer::Optimize(const Query &query, std::size_t page_count,
                              std::size_t row_count,
                              std::size_t tree_height,
                              bool has_index) const {
  if (query.type == QueryType::Insert) {
    return {query, AccessPath::DirectInsert, 0, 0,
            "direct heap insert followed by primary-key index update", {}, {}};
  }
  if (query.type == QueryType::Join) {
    const TableStats same{row_count, page_count, tree_height};
    return OptimizeJoin(query, same, same);
  }
  const double seq_cost = std::max<std::size_t>(1, page_count);
  const double selectivity =
      row_count == 0 ? 1.0 : 1.0 / static_cast<double>(row_count);
  const double index_cost =
      has_index ? std::max<std::size_t>(1, tree_height) + selectivity
                : std::numeric_limits<double>::infinity();
  const auto path =
      index_cost <= seq_cost ? AccessPath::IndexScan
                             : AccessPath::SequentialScan;
  return {query, path, seq_cost, index_cost,
          path == AccessPath::IndexScan
              ? "index lookup chosen: tree height + one tuple fetch <= heap pages"
              : "sequential scan chosen: heap is cheaper than index traversal",
          {}, {}};
}

QueryPlan Optimizer::OptimizeJoin(const Query &query, const TableStats &left,
                                  const TableStats &right, bool left_has_index,
                                  bool right_has_index) const {
  const bool left_outer =
      left.row_count <= right.row_count || right.row_count == 0;
  const auto &outer = left_outer ? left : right;
  const auto &inner = left_outer ? right : left;
  const bool inner_has_index = left_outer ? right_has_index : left_has_index;

  const double outer_rows = static_cast<double>(std::max<std::size_t>(1, outer.row_count));
  const double nested_loop_cost =
      outer_rows * static_cast<double>(std::max<std::size_t>(1, inner.page_count));
  const double index_nested_loop_cost =
      inner_has_index
          ? outer_rows *
                static_cast<double>(std::max<std::size_t>(1, inner.tree_height) + 1)
          : std::numeric_limits<double>::infinity();
  const AccessPath path =
      index_nested_loop_cost <= nested_loop_cost ? AccessPath::IndexNestedLoopJoin
                                                 : AccessPath::NestedLoopJoin;
  const std::string outer_name = left_outer ? query.table : query.join_table;
  const std::string inner_name = left_outer ? query.join_table : query.table;
  return {query,
          path,
          nested_loop_cost,
          index_nested_loop_cost,
          "join order chosen by row count: outer=" + outer_name +
              ", inner=" + inner_name,
          outer_name,
          inner_name};
}

}  // namespace minidb
