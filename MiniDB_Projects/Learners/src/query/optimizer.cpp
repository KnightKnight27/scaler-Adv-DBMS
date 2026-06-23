#include "optimizer.h"
#include <algorithm>
#include <cmath>

CostBasedOptimizer::CostBasedOptimizer(PageManager& pm, std::unordered_map<std::string, TableStats>& stats)
    : page_manager(pm), table_stats(stats) {}

int CostBasedOptimizer::get_cardinality(const std::string& table_name) {
    auto it = table_stats.find(table_name);
    if (it != table_stats.end()) {
        return it->second.num_records;
    }
    // Fallback: estimate 20 records per page
    int num_pages = page_manager.get_num_pages(table_name);
    return std::max(1, num_pages * 20);
}

double CostBasedOptimizer::estimate_selectivity(const std::string& table_name, const Optional<WhereClause>& where) {
    if (!where.has_value()) {
        return 1.0;
    }

    std::string col = where->column;
    std::string op = where->op;
    
    // Resolve column prefix
    size_t dot_pos = col.find('.');
    if (dot_pos != std::string::npos) {
        col = col.substr(dot_pos + 1);
    }

    auto it = table_stats.find(table_name);
    bool is_pk = (it != table_stats.end() && it->second.pk_column == col);

    if (op == "=") {
        if (is_pk) {
            int card = get_cardinality(table_name);
            return 1.0 / std::max(1, card);
        } else {
            return 0.1;
        }
    } else if (op == ">" || op == "<" || op == ">=" || op == "<=") {
        return 0.3;
    } else if (op == "!=") {
        return 0.9;
    }
    return 1.0;
}

double CostBasedOptimizer::cost_scan(const std::string& table_name, const Optional<WhereClause>& where, bool has_index, std::string& scan_type) {
    int num_pages = std::max(1, page_manager.get_num_pages(table_name));
    double table_scan_cost = (double)num_pages;

    if (!has_index || !where.has_value()) {
        scan_type = "TableScan";
        return table_scan_cost;
    }

    std::string col = where->column;
    size_t dot_pos = col.find('.');
    if (dot_pos != std::string::npos) {
        col = col.substr(dot_pos + 1);
    }

    auto it = table_stats.find(table_name);
    bool is_pk = (it != table_stats.end() && it->second.pk_column == col);

    if (!is_pk) {
        scan_type = "TableScan";
        return table_scan_cost;
    }

    // Index Scan cost = B+ Tree height (approx 3) + selectivity * cardinality (worst case page reads per key)
    double selectivity = estimate_selectivity(table_name, where);
    double matching_records = selectivity * get_cardinality(table_name);
    double index_scan_cost = 3.0 + matching_records;

    if (index_scan_cost < table_scan_cost) {
        scan_type = "IndexScan";
        return index_scan_cost;
    } else {
        scan_type = "TableScan";
        return table_scan_cost;
    }
}

double CostBasedOptimizer::select_join_order(
    const std::string& t1, const std::string& t2, 
    const JoinClause& join_cond,
    const Optional<WhereClause>& t1_where, 
    const Optional<WhereClause>& t2_where,
    bool t1_has_idx, bool t2_has_idx,
    std::string& outer_table, std::string& inner_table
) {
    std::string t1_scan_type, t2_scan_type;
    double t1_scan_cost = cost_scan(t1, t1_where, t1_has_idx, t1_scan_type);
    double t2_scan_cost = cost_scan(t2, t2_where, t2_has_idx, t2_scan_type);

    double t1_cardinality = get_cardinality(t1) * estimate_selectivity(t1, t1_where);
    double t2_cardinality = get_cardinality(t2) * estimate_selectivity(t2, t2_where);

    // Cost = cost(outer) + cardinality(outer) * cost(inner)
    double cost_t1_outer = t1_scan_cost + t1_cardinality * t2_scan_cost;
    double cost_t2_outer = t2_scan_cost + t2_cardinality * t1_scan_cost;

    if (cost_t1_outer <= cost_t2_outer) {
        outer_table = t1;
        inner_table = t2;
        return cost_t1_outer;
    } else {
        outer_table = t2;
        inner_table = t1;
        return cost_t2_outer;
    }
}
