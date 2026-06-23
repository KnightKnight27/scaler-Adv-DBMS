#include "execution/optimizer.h"

#include <cmath>
#include <stdexcept>

namespace minidb {

Optimizer::Optimizer(Catalog* catalog) : catalog_(catalog) {
    if (catalog_ == nullptr) {
        throw std::invalid_argument("catalog must not be null");
    }
}

std::unique_ptr<PlanNode> Optimizer::Optimize(const Statement& statement) {
    switch (statement.type) {
        case StatementType::SELECT:
            return OptimizeSelect(statement.select);
        case StatementType::INSERT: {
            auto plan = std::make_unique<PlanNode>();
            plan->type = PlanType::INSERT;
            plan->table = statement.insert.table;
            plan->insert.table = statement.insert.table;
            plan->insert.columns = statement.insert.columns;
            plan->insert.values = statement.insert.values;
            return plan;
        }
        case StatementType::DELETE: {
            auto plan = std::make_unique<PlanNode>();
            plan->type = PlanType::DELETE;
            plan->table = statement.delete_stmt.table;
            plan->delete_plan.table = statement.delete_stmt.table;
            plan->delete_plan.predicate = CloneExpr(statement.delete_stmt.where.get());
            return plan;
        }
    }
    throw std::runtime_error("Unsupported statement");
}

std::unique_ptr<PlanNode> Optimizer::OptimizeSelect(const SelectStatement& select) {
    std::unique_ptr<PlanNode> source;

    if (select.join) {
        auto join_plan = std::make_unique<PlanNode>();
        join_plan->type = PlanType::NESTED_LOOP_JOIN;
        join_plan->join.left_table = select.table;
        join_plan->join.right_table = select.join->table;
        join_plan->join.left_column = select.join->left_column;
        join_plan->join.right_column = select.join->right_column;

        auto left_scan = std::make_unique<PlanNode>();
        left_scan->type = PlanType::SEQ_SCAN;
        left_scan->table = select.table;

        auto right_scan = std::make_unique<PlanNode>();
        right_scan->type = PlanType::SEQ_SCAN;
        right_scan->table = select.join->table;

        join_plan->children.push_back(std::move(left_scan));
        join_plan->children.push_back(std::move(right_scan));
        source = std::move(join_plan);

        if (select.where) {
            auto filter = std::make_unique<PlanNode>();
            filter->type = PlanType::FILTER;
            filter->filter.predicate = CloneExpr(select.where.get());
            filter->children.push_back(std::move(source));
            source = std::move(filter);
        }
    } else if (select.where) {
        source = OptimizeScanWithFilter(select.table, CloneExpr(select.where.get()));
    } else {
        auto scan = std::make_unique<PlanNode>();
        scan->type = PlanType::SEQ_SCAN;
        scan->table = select.table;
        source = std::move(scan);
    }

    auto project = std::make_unique<PlanNode>();
    project->type = PlanType::PROJECT;
    project->table = select.table;
    project->project.column = select.column;
    project->children.push_back(std::move(source));
    return project;
}

std::unique_ptr<PlanNode> Optimizer::OptimizeScanWithFilter(const std::string& table,
                                                            std::unique_ptr<Expr> predicate) {
    if (predicate == nullptr) {
        auto scan = std::make_unique<PlanNode>();
        scan->type = PlanType::SEQ_SCAN;
        scan->table = table;
        return scan;
    }

    if (const auto* binary = dynamic_cast<const BinaryExpr*>(predicate.get())) {
        if (binary->op == "OR") {
            std::string left_col;
            std::string left_op;
            int64_t left_bound = 0;
            std::string right_col;
            std::string right_op;
            int64_t right_bound = 0;

            const bool left_simple =
                TryExtractPredicate(binary->left.get(), &left_col, &left_op, &left_bound);
            const bool right_simple =
                TryExtractPredicate(binary->right.get(), &right_col, &right_op, &right_bound);

            const double seq_cost = EstimateCost(PlanType::SEQ_SCAN, table, "", "", 0);

            if (left_simple && catalog_->HasIndex(table, left_col)) {
                const double index_cost =
                    EstimateCost(PlanType::INDEX_SCAN, table, left_col, left_op, left_bound);
                if (index_cost < seq_cost) {
                    auto index_scan = std::make_unique<PlanNode>();
                    index_scan->type = PlanType::INDEX_SCAN;
                    index_scan->table = table;
                    index_scan->index_scan.column = left_col;
                    index_scan->index_scan.op = left_op;
                    index_scan->index_scan.bound = left_bound;

                    auto filter = std::make_unique<PlanNode>();
                    filter->type = PlanType::FILTER;
                    filter->filter.predicate = CloneExpr(predicate.get());
                    filter->children.push_back(std::move(index_scan));
                    return filter;
                }
            }
            if (right_simple && catalog_->HasIndex(table, right_col)) {
                const double index_cost =
                    EstimateCost(PlanType::INDEX_SCAN, table, right_col, right_op, right_bound);
                if (index_cost < seq_cost) {
                    auto index_scan = std::make_unique<PlanNode>();
                    index_scan->type = PlanType::INDEX_SCAN;
                    index_scan->table = table;
                    index_scan->index_scan.column = right_col;
                    index_scan->index_scan.op = right_op;
                    index_scan->index_scan.bound = right_bound;

                    auto filter = std::make_unique<PlanNode>();
                    filter->type = PlanType::FILTER;
                    filter->filter.predicate = CloneExpr(predicate.get());
                    filter->children.push_back(std::move(index_scan));
                    return filter;
                }
            }
        } else if (binary->op == "AND") {
            return BuildAccessPath(table, *binary);
        } else {
            return BuildAccessPath(table, *binary);
        }
    }

    auto scan = std::make_unique<PlanNode>();
    scan->type = PlanType::SEQ_SCAN;
    scan->table = table;

    auto filter = std::make_unique<PlanNode>();
    filter->type = PlanType::FILTER;
    filter->filter.predicate = std::move(predicate);
    filter->children.push_back(std::move(scan));
    return filter;
}

std::unique_ptr<PlanNode> Optimizer::BuildAccessPath(const std::string& table,
                                                     const BinaryExpr& predicate) {
    std::string column;
    std::string op;
    int64_t bound = 0;

    if (TryExtractPredicate(&predicate, &column, &op, &bound) &&
        catalog_->HasIndex(table, column)) {
        const double index_cost = EstimateCost(PlanType::INDEX_SCAN, table, column, op, bound);
        const double seq_cost = EstimateCost(PlanType::SEQ_SCAN, table, "", "", 0);
        if (index_cost <= seq_cost) {
            auto index_scan = std::make_unique<PlanNode>();
            index_scan->type = PlanType::INDEX_SCAN;
            index_scan->table = table;
            index_scan->index_scan.column = column;
            index_scan->index_scan.op = op;
            index_scan->index_scan.bound = bound;

            if (op == "=") {
                return index_scan;
            }

            auto filter = std::make_unique<PlanNode>();
            filter->type = PlanType::FILTER;
            filter->filter.predicate = CloneExpr(&predicate);
            filter->children.push_back(std::move(index_scan));
            return filter;
        }
    }

    auto scan = std::make_unique<PlanNode>();
    scan->type = PlanType::SEQ_SCAN;
    scan->table = table;

    auto filter = std::make_unique<PlanNode>();
    filter->type = PlanType::FILTER;
    filter->filter.predicate = CloneExpr(&predicate);
    filter->children.push_back(std::move(scan));
    return filter;
}

bool Optimizer::TryExtractPredicate(const Expr* expr, std::string* column, std::string* op,
                                    int64_t* bound) const {
    const auto* binary = dynamic_cast<const BinaryExpr*>(expr);
    if (binary == nullptr) {
        return false;
    }
    if (binary->op == "OR" || binary->op == "AND") {
        return false;
    }

    const auto* left_col = dynamic_cast<const ColumnRefExpr*>(binary->left.get());
    const auto* right_lit = dynamic_cast<const LiteralExpr*>(binary->right.get());
    if (left_col == nullptr || right_lit == nullptr || right_lit->value.type != ValueType::INT) {
        return false;
    }

    *column = left_col->name;
    *op = binary->op;
    *bound = right_lit->value.int_val;
    return true;
}

double Optimizer::EstimateCost(PlanType type, const std::string& table,
                               const std::string& column, const std::string& op,
                               int64_t bound) const {
    const int64_t cardinality = catalog_->EstimateTableCardinality(table);
    if (type == PlanType::SEQ_SCAN) {
        return static_cast<double>(cardinality);
    }

    const double selectivity = catalog_->EstimateSelectivity(column, op, bound, cardinality);
    const double log_factor = cardinality > 0 ? std::log2(static_cast<double>(cardinality) + 1.0)
                                              : 1.0;
    return log_factor + selectivity * static_cast<double>(cardinality);
}

}  // namespace minidb
