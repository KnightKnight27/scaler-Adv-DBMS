#include "optimizer/optimizer.h"
#include <iostream>

namespace minidb {

Optimizer::Optimizer(ExecutorContext *ctx) : ctx_(ctx) {}

void Optimizer::UpdateStats(const std::string &table, const TableStats &stats) {
    stats_[table] = stats;
}

double Optimizer::EstimateSelectivity(const std::string &table, const std::string &col, const std::string &op) {
    if (stats_.find(table) == stats_.end()) return 1.0;
    
    auto &s = stats_[table];
    if (op == "=") {
        if (s.distinct_values.find(col) != s.distinct_values.end()) {
            int dv = s.distinct_values[col];
            return dv > 0 ? 1.0 / dv : 1.0;
        }
    } else if (op == ">" || op == "<" || op == ">=" || op == "<=") {
        return 0.3; // Default range selectivity
    }
    
    return 1.0;
}

double Optimizer::EstimateSeqScanCost(const std::string &table) {
    int rows = 1000;
    if (stats_.find(table) != stats_.end()) rows = stats_[table].rows;
    // N_pages * IO_Cost (Assuming ~40 tuples per page, IO_Cost = 1.0)
    return (rows / 40.0) * 1.0;
}

double Optimizer::EstimateIndexScanCost(const std::string &table) {
    int rows = 1000;
    if (stats_.find(table) != stats_.end()) rows = stats_[table].rows;
    // TreeHeight + K * IO_Cost
    return 3.0 + 1.0; // Assume height 3
}

double Optimizer::EstimateJoinCost(double left_cost, double right_cost, int left_rows) {
    return left_cost + left_rows * right_cost;
}

std::unique_ptr<Operator> Optimizer::Optimize(const ParsedStatement &stmt) {
    if (stmt.type == StatementType::SELECT) {
        if (stmt.has_join) {
            double cost_A = EstimateSeqScanCost(stmt.table_name);
            double cost_B = EstimateSeqScanCost(stmt.join_table);
            int rows_A = stats_.count(stmt.table_name) ? stats_[stmt.table_name].rows : 1000;
            int rows_B = stats_.count(stmt.join_table) ? stats_[stmt.join_table].rows : 1000;
            
            double cost_A_B = EstimateJoinCost(cost_A, cost_B, rows_A);
            double cost_B_A = EstimateJoinCost(cost_B, cost_A, rows_B);
            
            if (cost_A_B <= cost_B_A) {
                std::cout << "Optimizer chose Left-Deep Join: " << stmt.table_name << " -> " << stmt.join_table << " (Cost: " << cost_A_B << ")\n";
                auto left_scan = std::make_unique<SeqScan>(ctx_, stmt.table_name);
                auto right_scan = std::make_unique<SeqScan>(ctx_, stmt.join_table);
                return std::make_unique<NestedLoopJoin>(ctx_, std::move(left_scan), std::move(right_scan),
                                                        stmt.table_name, stmt.join_table, 
                                                        stmt.join_cond_left, stmt.join_cond_right);
            } else {
                std::cout << "Optimizer chose Left-Deep Join: " << stmt.join_table << " -> " << stmt.table_name << " (Cost: " << cost_B_A << ")\n";
                auto left_scan = std::make_unique<SeqScan>(ctx_, stmt.join_table);
                auto right_scan = std::make_unique<SeqScan>(ctx_, stmt.table_name);
                return std::make_unique<NestedLoopJoin>(ctx_, std::move(left_scan), std::move(right_scan),
                                                        stmt.join_table, stmt.table_name, 
                                                        stmt.join_cond_right, stmt.join_cond_left);
            }
        } else {
            if (stmt.has_where && stmt.where_column == "id" && stmt.where_op == "=") {
                double seq_cost = EstimateSeqScanCost(stmt.table_name);
                double idx_cost = EstimateIndexScanCost(stmt.table_name);
                if (idx_cost < seq_cost) {
                    std::cout << "Optimizer chose IndexScan on " << stmt.table_name << " (Cost: " << idx_cost << ")\n";
                    return std::make_unique<IndexScan>(ctx_, stmt.table_name, std::stoi(stmt.where_value));
                } else {
                    std::cout << "Optimizer chose SeqScan on " << stmt.table_name << " (Cost: " << seq_cost << ")\n";
                    return std::make_unique<SeqScan>(ctx_, stmt.table_name, stmt.where_column, stmt.where_op, stmt.where_value);
                }
            } else {
                double seq_cost = EstimateSeqScanCost(stmt.table_name);
                std::cout << "Optimizer chose SeqScan on " << stmt.table_name << " (Cost: " << seq_cost << ")\n";
                return std::make_unique<SeqScan>(ctx_, stmt.table_name, stmt.where_column, stmt.where_op, stmt.where_value);
            }
        }
    }
    return nullptr;
}

} // namespace minidb
