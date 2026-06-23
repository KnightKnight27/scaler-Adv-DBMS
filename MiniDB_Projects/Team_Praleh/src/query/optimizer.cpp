#include "query/optimizer.h"
#include <iostream>

namespace minidb {

static double EstimateSelectivity(const Statement& stmt, size_t total_records, const std::vector<Record>& sample_data) {
    if (total_records == 0) return 0.0;
    if (stmt.where_op == OpType::EQUAL) {
        return 1.0 / total_records;
    } else if (stmt.where_op == OpType::GREATER) {
        if (sample_data.empty()) return 0.3; // fallback
        int32_t min_v = (stmt.where_col == ColType::ID) ? sample_data[0].id : sample_data[0].value;
        int32_t max_v = min_v;
        for (const auto& r : sample_data) {
            int32_t val = (stmt.where_col == ColType::ID) ? r.id : r.value;
            if (val < min_v) min_v = val;
            if (val > max_v) max_v = val;
        }
        if (max_v <= min_v) return 0.5;
        if (stmt.where_val >= max_v) return 0.0;
        if (stmt.where_val <= min_v) return 1.0;
        return static_cast<double>(max_v - stmt.where_val) / (max_v - min_v);
    }
    return 1.0;
}

std::string Optimizer::SelectPlan(const Statement& stmt, size_t total_records, const std::vector<Record>& sample_data) const {
    if (stmt.type == StmtType::SELECT_JOIN) {
        double pages = static_cast<double>(total_records) / 340.0 + 1.0;
        double cost_nlj = pages + total_records * pages;
        double cost_inlj = pages + total_records * 2.0; // 2 page reads per index lookup average
        
        std::cout << "  [Optimizer] Cost NLJ = " << cost_nlj << ", Cost INLJ = " << cost_inlj << "\n";
        if (cost_inlj < cost_nlj) {
            return "INDEX_JOIN";
        } else {
            return "NESTED_LOOP_JOIN";
        }
    }
    
    if (stmt.type == StmtType::SELECT) {
        if (stmt.id != -1 && stmt.where_col == ColType::NONE) {
            return "POINT_INDEX_SCAN";
        }
        if (stmt.where_col == ColType::ID && stmt.where_op == OpType::EQUAL) {
            return "POINT_INDEX_SCAN";
        }
        if (stmt.where_col == ColType::VALUE && stmt.where_op == OpType::EQUAL) {
            double sel = 1.0 / (total_records > 0 ? total_records : 1);
            std::cout << "  [Optimizer] Estimated Selectivity = " << sel << "\n";
            return "TABLE_SCAN";
        }
        if (stmt.where_op == OpType::GREATER) {
            double sel = EstimateSelectivity(stmt, total_records, sample_data);
            std::cout << "  [Optimizer] Estimated Selectivity = " << sel << "\n";
            if (stmt.where_col == ColType::ID && sel < 0.4) {
                return "INDEX_SCAN_FILTER";
            }
            return "TABLE_SCAN";
        }
    }
    return "TABLE_SCAN";
}

} // namespace minidb
