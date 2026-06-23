#ifndef OPTIMIZER_H
#define OPTIMIZER_H

#include "parser.h"
#include "../storage/page_manager.h"
#include <string>
#include <unordered_map>

struct TableStats {
    int num_records{0};
    std::string pk_column;

    TableStats() = default;
    TableStats(int num, const std::string& pk) : num_records(num), pk_column(pk) {}
};

class CostBasedOptimizer {
private:
    PageManager& page_manager;
    std::unordered_map<std::string, TableStats>& table_stats;

public:
    CostBasedOptimizer(PageManager& pm, std::unordered_map<std::string, TableStats>& stats);

    int get_cardinality(const std::string& table_name);
    double estimate_selectivity(const std::string& table_name, const Optional<WhereClause>& where);
    double cost_scan(const std::string& table_name, const Optional<WhereClause>& where, bool has_index, std::string& scan_type);
    double select_join_order(
        const std::string& t1, const std::string& t2, 
        const JoinClause& join_cond,
        const Optional<WhereClause>& t1_where, 
        const Optional<WhereClause>& t2_where,
        bool t1_has_idx, bool t2_has_idx,
        std::string& outer_table, std::string& inner_table
    );
};

#endif
