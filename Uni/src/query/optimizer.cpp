#include "query/optimizer.h"
#include <algorithm>

double Optimizer::EstimateSelectivity(const std::string& table_name, const SQLWhereCondition& condition) const {
    if (!condition.has_condition) return 1.0;

    auto it = stats_.find(table_name);
    if (it == stats_.end()) return 0.05; // Fallback default selectivity

    const auto& table_stats = it->second;
    
    // Strip table prefix if any
    std::string col = condition.column;
    size_t dot = col.find('.');
    if (dot != std::string::npos) {
        col = col.substr(dot + 1);
    }

    if (condition.op == "=") {
        auto dist_it = table_stats.distinct_values.find(col);
        if (dist_it != table_stats.distinct_values.end() && dist_it->second > 0) {
            return 1.0 / dist_it->second;
        }
        return 0.05; // Default 5%
    } else if (condition.op == ">" || condition.op == "<") {
        return 0.30; // Default 30% range selectivity
    } else if (condition.op == "!=") {
        auto dist_it = table_stats.distinct_values.find(col);
        if (dist_it != table_stats.distinct_values.end() && dist_it->second > 0) {
            return 1.0 - (1.0 / dist_it->second);
        }
        return 0.95;
    }

    return 1.0;
}

bool Optimizer::ChooseIndexScan(const std::string& table_name,
                                 const SQLWhereCondition& condition,
                                 double& seq_cost,
                                 double& idx_cost) const {
    auto it = stats_.find(table_name);
    if (it == stats_.end()) {
        seq_cost = 9999.0;
        idx_cost = 9999.0;
        return false; // Default to table scan if no stats
    }

    const auto& table_stats = it->second;

    // Table scan cost: read all pages + scan all records in CPU
    seq_cost = table_stats.num_pages * IO_COST + table_stats.num_records * CPU_COST;

    // Index scan cost: height of tree + matching entries fetched + CPU filter cost
    double selectivity = EstimateSelectivity(table_name, condition);
    double matching_records = selectivity * table_stats.num_records;
    
    // Each matching record requires fetching its data page from disk (worst-case unbuffered)
    idx_cost = (table_stats.tree_height + matching_records) * IO_COST + matching_records * CPU_COST;

    return idx_cost < seq_cost;
}

std::pair<std::string, std::string> Optimizer::OrderJoin(const std::string& table_a, const std::string& table_b) const {
    int recs_a = 9999;
    int recs_b = 9999;

    auto it_a = stats_.find(table_a);
    if (it_a != stats_.end()) {
        recs_a = it_a->second.num_records;
    }

    auto it_b = stats_.find(table_b);
    if (it_b != stats_.end()) {
        recs_b = it_b->second.num_records;
    }

    // Outer table should be the smaller one to minimize join executions
    if (recs_a <= recs_b) {
        return {table_a, table_b};
    } else {
        return {table_b, table_a};
    }
}
