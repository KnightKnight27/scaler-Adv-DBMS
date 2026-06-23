#include "optimizer/optimizer.h"

#include <algorithm>

namespace minidb {

Optimizer::Optimizer(double table_page_count) : table_pages_(table_page_count) {}

double Optimizer::EstimateSelectivity(const Predicate& pred) const {
    (void)pred;
    return 0.1;
}

double Optimizer::ScanCost(double pages) const { return pages; }

double Optimizer::IndexCost(double selectivity) const {
    return 3.0 + selectivity * table_pages_;
}

double Optimizer::EstimateTablePages(const Catalog* catalog,
                                     const std::string& table) const {
    if (!catalog) return table_pages_;
    auto* heap = catalog->GetHeapFile(table);
    if (!heap) return table_pages_;
    int pages = heap->last_page_id() - heap->first_page_id() + 1;
    return static_cast<double>(std::max(1, pages));
}

std::shared_ptr<PlanNode> Optimizer::BuildScanPlan(const std::string& table, double pages,
                                                   const std::vector<Predicate>& preds) const {
    auto scan = std::make_shared<PlanNode>();
    scan->type = PlanType::SEQ_SCAN;
    scan->table = table;
    scan->predicates = preds;
    scan->estimated_cost = ScanCost(pages);
    scan->estimated_rows = pages * 100.0;
    return scan;
}

std::shared_ptr<PlanNode> Optimizer::Optimize(const SelectStmt& stmt,
                                              const Catalog* catalog) const {
    if (stmt.tables.empty()) {
        throw std::runtime_error("No tables in SELECT");
    }
    if (!stmt.aggregates.empty() && !stmt.joins.empty()) {
        throw std::runtime_error("Aggregates with JOIN are not supported");
    }

    if (!stmt.aggregates.empty()) {
        double pages = EstimateTablePages(catalog, stmt.tables.front());
        auto scan = BuildScanPlan(stmt.tables.front(), pages, stmt.predicates);

        auto agg = std::make_shared<PlanNode>();
        agg->type = PlanType::AGGREGATE;
        agg->left = scan;
        agg->table = stmt.tables.front();
        agg->aggregates = stmt.aggregates;
        agg->group_by = stmt.group_by;
        agg->project_columns = stmt.columns;
        agg->estimated_cost = scan->estimated_cost + 1.0;
        agg->estimated_rows = stmt.group_by.empty() ? 1.0 : scan->estimated_rows * 0.5;
        return agg;
    }

    if (stmt.joins.empty()) {
        auto root = std::make_shared<PlanNode>();
        bool has_eq_pred = !stmt.predicates.empty() &&
                           stmt.predicates.front().op == CompareOp::EQ;
        auto schema = catalog ? catalog->GetTable(stmt.tables.front()) : std::nullopt;
        bool indexed_col = schema && has_eq_pred &&
                           schema->IsColumnIndexed(stmt.predicates.front().column);
        double pages = EstimateTablePages(catalog, stmt.tables.front());
        if (has_eq_pred && indexed_col) {
            root->type = PlanType::INDEX_SCAN;
            root->index_column = stmt.predicates.front().column;
            root->estimated_cost = IndexCost(EstimateSelectivity(stmt.predicates.front()));
        } else {
            root->type = PlanType::SEQ_SCAN;
            root->estimated_cost = ScanCost(pages);
        }
        root->table = stmt.tables.front();
        root->predicates = stmt.predicates;
        root->project_columns = stmt.columns;
        root->estimated_rows = pages * 100 * EstimateSelectivity(
            stmt.predicates.empty() ? Predicate{} : stmt.predicates.front());
        return root;
    }

    JoinSpec join_spec = stmt.joins.front();
    double left_pages = EstimateTablePages(catalog, join_spec.left_table);
    double right_pages = EstimateTablePages(catalog, join_spec.right_table);
    if (catalog) {
        if (auto* left_heap = catalog->GetHeapFile(join_spec.left_table)) {
            left_pages = static_cast<double>(std::max<std::size_t>(1, left_heap->ScanAll().size()));
        }
        if (auto* right_heap = catalog->GetHeapFile(join_spec.right_table)) {
            right_pages =
                static_cast<double>(std::max<std::size_t>(1, right_heap->ScanAll().size()));
        }
    }

    auto left = BuildScanPlan(join_spec.left_table, left_pages, {});
    auto right = BuildScanPlan(join_spec.right_table, right_pages, {});

    if (right_pages < left_pages) {
        std::swap(left, right);
        std::swap(join_spec.left_table, join_spec.right_table);
        std::swap(join_spec.left_col, join_spec.right_col);
    }

    auto join = std::make_shared<PlanNode>();
    join->type = PlanType::NESTED_LOOP_JOIN;
    join->left = left;
    join->right = right;
    join->join = join_spec;
    join->predicates = stmt.predicates;
    join->project_columns = stmt.columns;
    join->estimated_cost = left->estimated_cost + right->estimated_cost +
                           left->estimated_rows * right->estimated_rows * 0.001;
    join->estimated_rows = left->estimated_rows * right->estimated_rows * 0.1;
    return join;
}

}  // namespace minidb
