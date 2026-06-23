#include "optimizer.h"

#include <stdexcept>

namespace minidb {

std::unique_ptr<PlanNode> Optimizer::scanFor(const std::string& table,
                                             const std::vector<Condition>& where,
                                             const Condition** consumed) {
    *consumed = nullptr;
    TableInfo* info = catalog_.get(table);
    if (!info) throw std::runtime_error("unknown table '" + table + "'");
    const std::string& pk = info->schema.columns[0].name;

    for (const Condition& c : where) {
        if (c.op == "=" && c.hasLiteral) {
            bool onPk = c.left.name == pk && (c.left.table.empty() || c.left.table == table);
            if (onPk && info->isIndexed()) {
                auto node = std::make_unique<PlanNode>();
                node->kind = PlanKind::IndexScan;
                node->table = table;
                node->key = c.literal;
                node->estRows = 1;
                *consumed = &c;
                return node;
            }
        }
    }
    auto node = std::make_unique<PlanNode>();
    node->kind = PlanKind::SeqScan;
    node->table = table;
    node->estRows = info->rowCount;
    return node;
}

std::unique_ptr<PlanNode> Optimizer::optimize(const SelectStmt& stmt) {
    std::unique_ptr<PlanNode> plan;

    if (!stmt.hasJoin) {
        const Condition* consumed = nullptr;
        plan = scanFor(stmt.table, stmt.where, &consumed);
        std::vector<Condition> residual;
        for (const Condition& c : stmt.where)
            if (&c != consumed) residual.push_back(c);
        if (!residual.empty()) {
            auto f = std::make_unique<PlanNode>();
            f->kind = PlanKind::Filter;
            f->conditions = residual;
            f->child = std::move(plan);
            plan = std::move(f);
        }
    } else {
        TableInfo* leftInfo = catalog_.get(stmt.table);
        TableInfo* rightInfo = catalog_.get(stmt.joinTable);
        if (!leftInfo || !rightInfo) throw std::runtime_error("unknown table in join");

        const Condition* c1 = nullptr;
        const Condition* c2 = nullptr;
        auto leftBase = scanFor(stmt.table, stmt.where, &c1);
        auto rightBase = scanFor(stmt.joinTable, stmt.where, &c2);

        auto join = std::make_unique<PlanNode>();
        join->kind = PlanKind::Join;
        join->joinCond = stmt.joinCond;
        // Smaller table becomes the inner (buffered) relation.
        if (rightInfo->rowCount <= leftInfo->rowCount) {
            join->left = std::move(leftBase);
            join->right = std::move(rightBase);
        } else {
            join->left = std::move(rightBase);
            join->right = std::move(leftBase);
        }
        plan = std::move(join);

        std::vector<Condition> residual;
        for (const Condition& c : stmt.where)
            if (&c != c1 && &c != c2) residual.push_back(c);
        if (!residual.empty()) {
            auto f = std::make_unique<PlanNode>();
            f->kind = PlanKind::Filter;
            f->conditions = residual;
            f->child = std::move(plan);
            plan = std::move(f);
        }
    }

    if (!stmt.columns.empty()) {
        auto p = std::make_unique<PlanNode>();
        p->kind = PlanKind::Project;
        p->columns = stmt.columns;
        p->child = std::move(plan);
        plan = std::move(p);
    }
    return plan;
}

std::string explainPlan(const PlanNode* node, int depth) {
    std::string pad(depth * 2, ' ');
    switch (node->kind) {
        case PlanKind::SeqScan:
            return pad + "SeqScan(" + node->table + ")  [reads all ~" +
                   std::to_string(node->estRows) + " rows]";
        case PlanKind::IndexScan:
            return pad + "IndexScan(" + node->table + ", key=" + node->key.toString() +
                   ")  [B+ tree lookup, O(log n)]";
        case PlanKind::Filter:
            return pad + "Filter(" + std::to_string(node->conditions.size()) + " cond)\n" +
                   explainPlan(node->child.get(), depth + 1);
        case PlanKind::Project: {
            std::string cols;
            for (size_t i = 0; i < node->columns.size(); ++i)
                cols += (i ? ", " : "") + node->columns[i].name;
            return pad + "Project(" + cols + ")\n" + explainPlan(node->child.get(), depth + 1);
        }
        case PlanKind::Join:
            return pad + "NestedLoopJoin\n" + explainPlan(node->left.get(), depth + 1) + "\n" +
                   explainPlan(node->right.get(), depth + 1);
    }
    return pad + "?";
}

}  // namespace minidb
