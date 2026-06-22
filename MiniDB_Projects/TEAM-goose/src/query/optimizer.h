#pragma once

#include "common/types.h"
#include "parser.h"
#include "indexing/bplustree.h"
#include <unordered_map>
#include <set>
#include <algorithm>
#include <cmath>

namespace minidb {

class LSMEngine;  // forward decl

// optimizer — cost-based query optimizer
// implements:
//   1. selectivity estimation (heuristic-based)
//   2. join order selection (left-deep plans, greedy)
//   3. access path selection: table scan vs index scan

struct TableStats {
    size_t row_count = 0;
    // per-column: number of distinct values (for selectivity estimation)
    std::unordered_map<std::string, size_t> distinct_counts;
};

struct OptimizerConfig {
    double index_scan_cost_factor  = 0.1;   // index scan is ~10% of table scan cost
    double table_scan_cost_per_row = 1.0;   // baseline cost per row
};

class Optimizer {
public:
    Optimizer() = default;

    // register table statistics so the optimizer can make decisions.
    void set_table_stats(const std::string& table, const TableStats& stats) {
        _stats[table] = stats;
    }

    // ---- selectivity estimation -------------------------------------------
    // estimate selectivity of where col op val on a table.
    // returns fraction of rows expected to match (0.0 to 1.0).
    double estimate_selectivity(const std::string& table,
                                const std::string& col,
                                const std::string& op,
                                const Value& val);

    // ---- join order selection ---------------------------------------------
    // given a list of tables to join, return the optimal join order.
    // uses a simple greedy heuristic: join the smallest tables first.
    std::vector<std::string> select_join_order(
        const std::vector<std::string>& tables);

    // ---- access path selection --------------------------------------------
    // decide whether to use a table scan or an index scan for a query.
    // returns true if an index scan should be preferred.
    bool prefer_index_scan(const std::string& table,
                           const std::string& col,
                           const WhereClause& where);

    // ---- cost estimation --------------------------------------------------
    // estimate the cost (in abstract units) of scanning a table.
    double estimate_scan_cost(const std::string& table);

    // estimate the cost of an index lookup + scan.
    double estimate_index_cost(const std::string& table,
                               const std::string& col,
                               const WhereClause& where);

private:
    std::unordered_map<std::string, TableStats> _stats;
    OptimizerConfig _config;
};

// implementation

inline double Optimizer::estimate_selectivity(const std::string& table,
                                               const std::string& col,
                                               const std::string& op,
                                               const Value& /*val*/) {
    auto it = _stats.find(table);
    if (it == _stats.end()) return 0.1; // unknown → assume 10%

    const auto& stats = it->second;
    if (stats.row_count == 0) return 0.0;

    auto dit = stats.distinct_counts.find(col);
    size_t distinct = (dit != stats.distinct_counts.end()) ? dit->second : stats.row_count;

    // equality selectivity: 1 / distinct
    if (op == "=")  return 1.0 / std::max(distinct, size_t(1));
    if (op == "!=") return 1.0 - (1.0 / std::max(distinct, size_t(1)));
    // range selectivity: rough 33% heuristic
    if (op == "<" || op == ">" || op == "<=" || op == ">=") return 0.33;

    return 0.1;
}

inline std::vector<std::string> Optimizer::select_join_order(
    const std::vector<std::string>& tables) {
    if (tables.size() <= 1) return tables;

    // greedy: sort by estimated row count (ascending)
    // smaller tables first = lower intermediate result sizes
    std::vector<std::pair<std::string, size_t>> sized;
    for (const auto& t : tables) {
        auto it = _stats.find(t);
        size_t rows = (it != _stats.end()) ? it->second.row_count : 100;
        sized.emplace_back(t, rows);
    }
    std::sort(sized.begin(), sized.end(),
              [](auto& a, auto& b) { return a.second < b.second; });

    std::vector<std::string> order;
    for (auto& [t, _] : sized) order.push_back(t);
    return order;
}

inline bool Optimizer::prefer_index_scan(const std::string& table,
                                          const std::string& /*col*/,
                                          const WhereClause& where) {
    if (!where.has_where) return false;

    auto it = _stats.find(table);
    if (it == _stats.end()) return false;

    double selectivity = estimate_selectivity(table, where.column, where.op, where.value);
    // use index only when selectivity is very low (< 5%)
    // this is a common heuristic in real databases.
    return selectivity < 0.05;
}

inline double Optimizer::estimate_scan_cost(const std::string& table) {
    auto it = _stats.find(table);
    if (it == _stats.end()) return 100.0;
    return _config.table_scan_cost_per_row * static_cast<double>(it->second.row_count);
}

inline double Optimizer::estimate_index_cost(const std::string& table,
                                              const std::string& col,
                                              const WhereClause& where) {
    double selectivity = estimate_selectivity(table, col, where.op, where.value);
    auto it = _stats.find(table);
    double rows = (it != _stats.end()) ? static_cast<double>(it->second.row_count) : 100.0;

    // b+ tree lookup: ~log(n) to find first entry, plus scan matching rows
    double btree_traversal = std::log2(std::max(rows, 1.0));
    double matching_rows   = selectivity * rows;
    return _config.index_scan_cost_factor * (btree_traversal + matching_rows);
}

} // namespace minidb
