#ifndef COST_BASED_OPTIMIZER_H
#define COST_BASED_OPTIMIZER_H

#include <vector>
#include <unordered_map>
#include <memory>
#include <string>
#include <cstdint>
#include <stdexcept>
#include <algorithm>
#include <cmath>

namespace minidb {

using table_id_t = uint8_t;
using JoinGroupBitmask = uint32_t;

/**
 * Metadata statistics containing page and tuple count statistics.
 */
struct TableStats {
    size_t page_count{0};
    size_t tuple_count{0};
    bool has_index{false};
};

/**
 * System catalog containing table statistics for optimizer cost estimation.
 */
class SystemCatalog {
public:
    void AddStats(table_id_t table, TableStats stats) {
        stats_[table] = stats;
    }

    const TableStats& GetStats(table_id_t table) const {
        auto it = stats_.find(table);
        if (it == stats_.end()) {
            throw std::runtime_error("Catalog: table stats not found for id: " + std::to_string(table));
        }
        return it->second;
    }

private:
    std::unordered_map<table_id_t, TableStats> stats_;
};

/**
 * High-level logical query containing participating table identifiers.
 */
struct LogicalQuerySpecification {
    std::vector<table_id_t> tables;
};

/**
 * Node within physical execution plan representation, noting estimated costs.
 */
struct PhysicalPlanNode {
    enum class PlanType { SEQ_SCAN, INDEX_SCAN, NESTED_LOOP_JOIN, HASH_JOIN };
    
    PlanType type;
    double estimated_cost;
    size_t estimated_cardinality;
    
    std::shared_ptr<PhysicalPlanNode> left_child;
    std::shared_ptr<PhysicalPlanNode> right_child; // strictly leaf scan for join types
    table_id_t target_table; // populated if scan type
};

/**
 * Cost-based optimizer calculating left-deep join order schedules using Selinger DP.
 */
class CostBasedOptimizer {
public:
    explicit CostBasedOptimizer(const SystemCatalog* catalog) : catalog_(catalog) {}

    std::shared_ptr<PhysicalPlanNode> OptimizeQuery(const LogicalQuerySpecification& query);

private:
    std::shared_ptr<PhysicalPlanNode> DetermineBestAccessPath(table_id_t table);
    
    double CalculateJoinCost(const PhysicalPlanNode& left, 
                             const PhysicalPlanNode& right, 
                             PhysicalPlanNode::PlanType join_type);

    size_t EstimateJoinCardinality(JoinGroupBitmask left_mask, 
                                   JoinGroupBitmask right_mask);

    const SystemCatalog* catalog_;
    std::unordered_map<JoinGroupBitmask, std::shared_ptr<PhysicalPlanNode>> memo_table_;
};

} // namespace minidb

#endif // COST_BASED_OPTIMIZER_H
