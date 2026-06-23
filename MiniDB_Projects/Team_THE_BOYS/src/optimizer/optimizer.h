#pragma once

#include <memory>
#include <string>
#include <vector>

#include "catalog/catalog.h"
#include "common/types.h"
#include "parser/sql_parser.h"

namespace minidb {

enum class PlanType {
    SEQ_SCAN,
    INDEX_SCAN,
    NESTED_LOOP_JOIN,
    AGGREGATE,
    FILTER,
    PROJECT
};

struct PlanNode {
    PlanType type;
    std::string table;
    std::string index_column;
    std::vector<Predicate> predicates;
    std::vector<std::string> project_columns;
    std::vector<AggregateExpr> aggregates;
    std::vector<std::string> group_by;
    std::shared_ptr<PlanNode> left;
    std::shared_ptr<PlanNode> right;
    JoinSpec join;
    double estimated_cost = 0.0;
    double estimated_rows = 0.0;
};

class Optimizer {
public:
    explicit Optimizer(double table_page_count = 100.0);

    std::shared_ptr<PlanNode> Optimize(const SelectStmt& stmt, const Catalog* catalog) const;
    double EstimateSelectivity(const Predicate& pred) const;

private:
    double table_pages_;

    double ScanCost(double pages) const;
    double IndexCost(double selectivity) const;
    double EstimateTablePages(const Catalog* catalog, const std::string& table) const;
    std::shared_ptr<PlanNode> BuildScanPlan(const std::string& table, double pages,
                                            const std::vector<Predicate>& preds) const;
};

}  // namespace minidb
