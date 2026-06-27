#include "optimizer/optimizer.h"
#include <stdexcept>

Optimizer::Optimizer(const Catalog& catalog) : catalog_(catalog) {}

std::optional<int> Optimizer::extract_pk_eq(const Expression* expr,
                                              const std::string& pk_col) const {
    auto* bin = dynamic_cast<const BinaryExpr*>(expr);
    if (!bin || bin->op != "=") return std::nullopt;

    auto* col = dynamic_cast<const ColumnRef*>(bin->left);
    auto* lit = dynamic_cast<const Literal*>(bin->right);

    if (!col || !lit) {
        col = dynamic_cast<const ColumnRef*>(bin->right);
        lit = dynamic_cast<const Literal*>(bin->left);
    }
    if (!col || !lit) return std::nullopt;
    if (col->col != pk_col) return std::nullopt;

    try { return std::stoi(lit->value); }
    catch (...) { return std::nullopt; }
}

QueryPlan Optimizer::plan(const SelectStmt& stmt) const {
    if (!catalog_.has_table(stmt.table))
        throw std::runtime_error("Table not found: " + stmt.table);

    const auto& schema = catalog_.get_schema(stmt.table);
    std::string pk_col = schema.columns.empty() ? "" : schema.columns[0].name;

    QueryPlan p;
    p.table  = stmt.table;
    p.filter = stmt.where;

    if (stmt.where && !stmt.join_table.empty() == false) {
        auto val = extract_pk_eq(stmt.where, pk_col);
        if (val) {
            p.scan   = ScanType::INDEX_SCAN;
            p.pk_val = *val;
            goto fill_join;
        }
    }

    p.scan = ScanType::TABLE_SCAN;

fill_join:
    if (!stmt.join_table.empty()) {
        p.has_join       = true;
        p.join_table     = stmt.join_table;
        p.join_left_col  = stmt.join_left_col;
        p.join_right_col = stmt.join_right_col;
        p.scan = ScanType::TABLE_SCAN;
    }

    return p;
}
