#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include "query/parser.h"

struct TableStats {
    int num_pages = 0;
    int num_records = 0;
    int tree_height = 3;
    std::unordered_map<std::string, int> distinct_values;
};

class Optimizer {
public:
    Optimizer() = default;

    void SetTableStats(const std::string& table_name, const TableStats& stats) {
        stats_[table_name] = stats;
    }

    const TableStats* GetStats(const std::string& table_name) const {
        auto it = stats_.find(table_name);
        return (it != stats_.end()) ? &it->second : nullptr;
    }

    double EstimateSelectivity(const std::string& table_name, const SQLWhereCondition& condition) const;

    bool ChooseIndexScan(const std::string& table_name,
                         const SQLWhereCondition& condition,
                         double& seq_cost,
                         double& idx_cost) const;

    std::pair<std::string, std::string> OrderJoin(const std::string& table_a, const std::string& table_b) const;

private:
    std::unordered_map<std::string, TableStats> stats_;
    
    const double IO_COST = 4.0;
    const double CPU_COST = 0.1;
};
