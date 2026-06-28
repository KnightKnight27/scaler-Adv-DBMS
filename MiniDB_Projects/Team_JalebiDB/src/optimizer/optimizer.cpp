#include "optimizer/optimizer.h"
#include <iostream>

namespace minidb {

Optimizer::Optimizer(Catalog *catalog) : catalog_(catalog) {}

void Optimizer::SetTableStats(const std::string &table, int num_tuples, int num_pages) {
    stats_[table] = {num_tuples, num_pages};
}

std::unique_ptr<PhysicalPlan> Optimizer::MakeScanPlan(const std::string &table, const std::string &where_col, WhereOp where_op, const Value &where_val) {
    auto plan = std::make_unique<PhysicalPlan>();
    plan->table_name = table;
    
    TableMetadata *meta = catalog_->GetTable(table);
    if (meta == nullptr) {
        plan->type = PlanType::SEQ_SCAN;
        return plan;
    }

    // Check if index exists on the WHERE column
    bool has_index = (!meta->pk_col.empty() && meta->pk_col == where_col && meta->root_page_id != INVALID_PAGE_ID);

    if (has_index && where_op == WhereOp::EQUALS) {
        // CBO Decision: Choose IndexScan vs SeqScan
        TableStats t_stats = stats_[table];
        
        // Cost estimation:
        // SeqScan reads all pages. Page I/O cost = 1.0 per page.
        double seq_cost = t_stats.num_pages * 1.0;

        // IndexScan traverses the B+ tree (height approx 2-3) and reads 1 data page.
        // Index Page I/O cost = 1.0. Random page read is slightly higher.
        double index_cost = 3.0 + 1.0; 

        if (index_cost < seq_cost) {
            plan->type = PlanType::INDEX_SCAN;
            plan->scan_col = where_col;
            plan->scan_op = where_op;
            plan->scan_val = where_val;
            return plan;
        }
    }

    // Default to SeqScan
    plan->type = PlanType::SEQ_SCAN;
    if (!where_col.empty()) {
        plan->scan_col = where_col;
        plan->scan_op = where_op;
        plan->scan_val = where_val;
    }
    return plan;
}

std::unique_ptr<PhysicalPlan> Optimizer::Optimize(const SQLStatement &stmt) {
    if (stmt.type == SQLStatementType::INSERT) {
        // INSERT does not require query planning
        return nullptr;
    }

    if (stmt.type == SQLStatementType::DELETE) {
        return MakeScanPlan(stmt.delete_table, stmt.where_col, stmt.where_op, stmt.where_val);
    }

    // SELECT statement
    if (stmt.join_table.empty()) {
        // Single table SELECT
        return MakeScanPlan(stmt.select_table, stmt.where_col, stmt.where_op, stmt.where_val);
    }

    // JOIN query SELECT
    // Generate scan plans for left and right tables
    std::string left_where_col = "";
    WhereOp left_where_op = WhereOp::NONE;
    Value left_where_val;

    std::string right_where_col = "";
    WhereOp right_where_op = WhereOp::NONE;
    Value right_where_val;

    // Check where the WHERE predicate belongs
    if (!stmt.where_col.empty()) {
        std::string col = stmt.where_col;
        size_t dot_pos = col.find('.');
        std::string prefix = "";
        if (dot_pos != std::string::npos) {
            prefix = col.substr(0, dot_pos);
            col = col.substr(dot_pos + 1);
        }

        if (prefix == stmt.select_table || prefix.empty()) {
            left_where_col = col;
            left_where_op = stmt.where_op;
            left_where_val = stmt.where_val;
        } else if (prefix == stmt.join_table) {
            right_where_col = col;
            right_where_op = stmt.where_op;
            right_where_val = stmt.where_val;
        }
    }

    auto left_scan = MakeScanPlan(stmt.select_table, left_where_col, left_where_op, left_where_val);
    auto right_scan = MakeScanPlan(stmt.join_table, right_where_col, right_where_op, right_where_val);

    // Join order selection using cost metrics:
    // Nested Loop Join Cost: Cost(Outer) + NumTuples(Outer) * Cost(Inner)
    int left_tuples = stats_[stmt.select_table].num_tuples;
    if (left_tuples == 0) left_tuples = 1; // fallback
    int right_tuples = stats_[stmt.join_table].num_tuples;
    if (right_tuples == 0) right_tuples = 1; // fallback

    double left_scan_cost = stats_[stmt.select_table].num_pages;
    double right_scan_cost = stats_[stmt.join_table].num_pages;

    // Plan A: Left is Outer, Right is Inner
    double cost_a = left_scan_cost + left_tuples * right_scan_cost;
    // Plan B: Right is Outer, Left is Inner
    double cost_b = right_scan_cost + right_tuples * left_scan_cost;

    auto join_plan = std::make_unique<PhysicalPlan>();
    join_plan->type = PlanType::NESTED_LOOP_JOIN;
    join_plan->join_col_left = stmt.join_col_left;
    join_plan->join_col_right = stmt.join_col_right;

    if (cost_a <= cost_b) {
        join_plan->left_plan = std::move(left_scan);
        join_plan->right_plan = std::move(right_scan);
    } else {
        // Swap: right table becomes outer (left child of NestLoopJoin)
        join_plan->left_plan = std::move(right_scan);
        join_plan->right_plan = std::move(left_scan);
    }

    return join_plan;
}

} // namespace minidb
