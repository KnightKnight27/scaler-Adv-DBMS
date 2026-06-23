#pragma once
#include "sql/parser.h"
#include "sql/executor.h"
#include <string>
#include <unordered_map>
#include <memory>

namespace minidb {

struct TableStats {
    int rows;
    std::unordered_map<std::string, int> distinct_values;
};

class Optimizer {
public:
    Optimizer(ExecutorContext *ctx);
    
    void UpdateStats(const std::string &table, const TableStats &stats);
    
    std::unique_ptr<Operator> Optimize(const ParsedStatement &stmt);

private:
    ExecutorContext *ctx_;
    std::unordered_map<std::string, TableStats> stats_;
    
    double EstimateSelectivity(const std::string &table, const std::string &col, const std::string &op);
    
    double EstimateSeqScanCost(const std::string &table);
    double EstimateIndexScanCost(const std::string &table);
    double EstimateJoinCost(double left_cost, double right_cost, int left_rows);
};

} // namespace minidb
