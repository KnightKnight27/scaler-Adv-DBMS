#pragma once

#include <memory>
#include <string>
#include <cmath>
#include <limits>
#include "execution/abstract_executor.h"
#include "execution/seq_scan_executor.h"
#include "execution/index_scan_executor.h"
#include "execution/filter_executor.h"
#include "storage/btree/bplus_tree.h"
#include "catalog/catalog.h"
#include "common/logger.h"

namespace minidb {

class Optimizer {
private:
    ExecutorContext* context_;

public:
    explicit Optimizer(ExecutorContext* context) : context_(context) {}

    // Generates an optimized physical execution plan for a Point Lookup query:
    // e.g., SELECT * FROM table WHERE column = 'search_key'
    std::unique_ptr<AbstractExecutor> OptimizePointLookup(
        table_oid_t table_oid, 
        const std::string& search_key, 
        uint32_t filter_col_idx,
        BPlusTree* index = nullptr,
        size_t estimated_table_size = 10000) 
    {
        // 1. Calculate Cost of a Sequential Scan + Filter
        // Cost model: O(N) where N is the number of rows. We must scan every single row.
        double seq_scan_cost = static_cast<double>(estimated_table_size);

        // 2. Calculate Cost of an Index Scan (if index is available)
        // Cost model: O(log N) + base tree traversal cost.
        double index_scan_cost = std::numeric_limits<double>::max();
        if (index != nullptr) {
            index_scan_cost = std::log2(estimated_table_size > 1 ? estimated_table_size : 2);
        }

        // 3. Compare costs and dynamically construct the most efficient pipeline
        if (index != nullptr && index_scan_cost < seq_scan_cost) {
            LOG_INFO("Optimizer chose IndexScan. Estimated Cost: " + std::to_string(index_scan_cost));
            return std::make_unique<IndexScanExecutor>(context_, table_oid, index, search_key);
        } else {
            LOG_INFO("Optimizer chose SeqScan + Filter. Estimated Cost: " + std::to_string(seq_scan_cost));
            auto seq_scan = std::make_unique<SeqScanExecutor>(context_, table_oid);
            return std::make_unique<FilterExecutor>(context_, std::move(seq_scan), filter_col_idx, search_key);
        }
    }
};

} // namespace minidb