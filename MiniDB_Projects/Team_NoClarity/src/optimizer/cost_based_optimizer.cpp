#include "optimizer/cost_based_optimizer.h"
#include <limits>
#include <iostream>

namespace minidb {

static constexpr double PAGE_IO_COST = 10.0;
static constexpr double TUPLE_CPU_COST = 0.1;
static constexpr double HASH_BUILD_FACTOR = 0.2;
static constexpr double HASH_PROBE_FACTOR = 0.1;

std::shared_ptr<PhysicalPlanNode> CostBasedOptimizer::DetermineBestAccessPath(table_id_t table) {
    const auto& stats = catalog_->GetStats(table);
    double seq_cost = stats.page_count * PAGE_IO_COST + stats.tuple_count * TUPLE_CPU_COST;
    
    double idx_cost = std::numeric_limits<double>::max();
    if (stats.has_index) {
        // B+ Tree indexing path cost model
        idx_cost = std::log2(stats.tuple_count + 1) * PAGE_IO_COST + (0.1 * stats.tuple_count) * TUPLE_CPU_COST;
    }

    auto node = std::make_shared<PhysicalPlanNode>();
    node->estimated_cardinality = stats.tuple_count;
    node->target_table = table;
    node->left_child = nullptr;
    node->right_child = nullptr;

    if (idx_cost < seq_cost) {
        node->type = PhysicalPlanNode::PlanType::INDEX_SCAN;
        node->estimated_cost = idx_cost;
    } else {
        node->type = PhysicalPlanNode::PlanType::SEQ_SCAN;
        node->estimated_cost = seq_cost;
    }
    return node;
}

double CostBasedOptimizer::CalculateJoinCost(const PhysicalPlanNode& left, 
                                             const PhysicalPlanNode& right, 
                                             PhysicalPlanNode::PlanType join_type) {
    double left_cost = left.estimated_cost;
    double right_cost = right.estimated_cost;

    // Right-child is always a base scan in left-deep tree
    table_id_t r_table = right.target_table;
    const auto& r_stats = catalog_->GetStats(r_table);
    double r_pages = r_stats.page_count;
    double r_tuples = r_stats.tuple_count;

    double left_card = left.estimated_cardinality;

    if (join_type == PhysicalPlanNode::PlanType::NESTED_LOOP_JOIN) {
        // Outer loop evaluates Left child. Inner loop scans Right base table.
        double exec_cost = left_card * r_pages * PAGE_IO_COST + (left_card * r_tuples) * TUPLE_CPU_COST;
        return left_cost + right_cost + exec_cost;
    } else if (join_type == PhysicalPlanNode::PlanType::HASH_JOIN) {
        // Build phase on Right relation + Probe phase on Left relation
        double build_probe_cost = r_tuples * HASH_BUILD_FACTOR + left_card * HASH_PROBE_FACTOR;
        // Cardinality match checks
        double join_match_card = left_card * r_tuples * 0.05;
        double exec_cost = build_probe_cost + join_match_card * TUPLE_CPU_COST;
        return left_cost + right_cost + exec_cost;
    }
    return std::numeric_limits<double>::max();
}

size_t CostBasedOptimizer::EstimateJoinCardinality(JoinGroupBitmask left_mask, 
                                                   JoinGroupBitmask right_mask) {
    size_t left_card = memo_table_[left_mask]->estimated_cardinality;
    size_t right_card = memo_table_[right_mask]->estimated_cardinality;
    size_t res = static_cast<size_t>(left_card * right_card * 0.05);
    return res < 1 ? 1 : res;
}

// Recursively generates bitmask subsets of a given size
static void GenerateSubsets(const std::vector<table_id_t>& tables, size_t index, size_t count, size_t target_size,
                            JoinGroupBitmask current_mask, std::vector<JoinGroupBitmask>& result) {
    if (count == target_size) {
        result.push_back(current_mask);
        return;
    }
    if (index >= tables.size()) {
        return;
    }
    // Include
    GenerateSubsets(tables, index + 1, count + 1, target_size, current_mask | (1 << tables[index]), result);
    // Exclude
    GenerateSubsets(tables, index + 1, count, target_size, current_mask, result);
}

std::shared_ptr<PhysicalPlanNode> CostBasedOptimizer::OptimizeQuery(const LogicalQuerySpecification& query) {
    memo_table_.clear();
    size_t K = query.tables.size();
    if (K == 0) {
        return nullptr;
    }

    // Stage 1: Size 1 subproblems
    for (table_id_t t : query.tables) {
        memo_table_[1 << t] = DetermineBestAccessPath(t);
    }

    // Stage 2: DP Iteration Loop (size 2 up to K)
    for (size_t S = 2; S <= K; ++S) {
        std::vector<JoinGroupBitmask> subsets;
        GenerateSubsets(query.tables, 0, 0, S, 0, subsets);

        for (JoinGroupBitmask subset : subsets) {
            auto best_subset_node = std::make_shared<PhysicalPlanNode>();
            best_subset_node->estimated_cost = std::numeric_limits<double>::max();

            // Split subset bitmask into LeftDeep partitions: LeftMask (S-1 tables) + RightMask (1 table)
            for (table_id_t t : query.tables) {
                JoinGroupBitmask right_mask = 1 << t;
                if ((subset & right_mask) != 0) {
                    JoinGroupBitmask left_mask = subset ^ right_mask;
                    if (left_mask != 0) {
                        auto left_child = memo_table_[left_mask];
                        auto right_child = memo_table_[right_mask];
                        
                        // Evaluate join options
                        std::vector<PhysicalPlanNode::PlanType> join_types{
                            PhysicalPlanNode::PlanType::NESTED_LOOP_JOIN,
                            PhysicalPlanNode::PlanType::HASH_JOIN
                        };

                        for (auto join_type : join_types) {
                            double cost = CalculateJoinCost(*left_child, *right_child, join_type);
                            if (cost < best_subset_node->estimated_cost) {
                                best_subset_node->type = join_type;
                                best_subset_node->estimated_cost = cost;
                                best_subset_node->estimated_cardinality = EstimateJoinCardinality(left_mask, right_mask);
                                best_subset_node->left_child = left_child;
                                best_subset_node->right_child = right_child;
                                best_subset_node->target_table = 0;
                            }
                        }
                    }
                }
            }
            memo_table_[subset] = best_subset_node;
        }
    }

    JoinGroupBitmask full_mask = 0;
    for (table_id_t t : query.tables) {
        full_mask |= (1 << t);
    }
    return memo_table_[full_mask];
}

} // namespace minidb
