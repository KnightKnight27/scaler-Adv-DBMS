#include "planner.h"

#include <stdexcept>

#include "optimizer.h"

namespace minidb {

namespace {
Value DefaultValue(TypeId t) {
    switch (t) {
        case TypeId::INTEGER: return Value::MakeInt(0);
        case TypeId::BIGINT:  return Value::MakeBigInt(0);
        case TypeId::BOOLEAN: return Value::MakeBool(false);
        case TypeId::VARCHAR: return Value::MakeVarchar("");
        default:              return Value();
    }
}

// Build a schema whose column names are qualified with the table name ("t.col").
Schema Qualify(const std::string& table, const Schema& s) {
    std::vector<Column> cols;
    for (const auto& c : s.Columns())
        cols.push_back({table + "." + c.name, c.type, false});
    return Schema(std::move(cols));
}
}  // namespace

PhysicalPlan Planner::Plan(Statement* stmt) {
    switch (stmt->kind) {
        case Statement::Kind::Select: return PlanSelect(static_cast<SelectStmt*>(stmt));
        case Statement::Kind::Insert: return PlanInsert(static_cast<InsertStmt*>(stmt));
        case Statement::Kind::Delete: return PlanDelete(static_cast<DeleteStmt*>(stmt));
        default: throw std::runtime_error("planner: unsupported statement");
    }
}

PhysicalPlan Planner::PlanSelect(SelectStmt* s) {
    TableInfo* t = cat_->GetTable(s->from_table);
    if (!t) throw std::runtime_error("no such table: " + s->from_table);

    std::unique_ptr<Executor> root;
    Schema cur_schema;
    std::string explain;

    if (s->join.present) {
        TableInfo* r = cat_->GetTable(s->join.table);
        if (!r) throw std::runtime_error("no such table: " + s->join.table);

        // Resolve each join column to its owning table + local index.
        auto resolve = [&](const std::string& cn) -> std::pair<TableInfo*, int> {
            auto dot = cn.find('.');
            if (dot != std::string::npos) {
                std::string pre = cn.substr(0, dot);
                if (pre == t->name) return {t, t->schema.GetColIdx(cn)};
                if (pre == r->name) return {r, r->schema.GetColIdx(cn)};
            }
            int it = t->schema.GetColIdx(cn);
            if (it >= 0) return {t, it};
            return {r, r->schema.GetColIdx(cn)};
        };
        auto a = resolve(s->join.left_col);
        auto b = resolve(s->join.right_col);

        // Smaller table becomes the outer (driving) relation.
        bool swap = Optimizer::ShouldSwapJoin(t->num_rows, r->num_rows);
        TableInfo* outer = swap ? r : t;
        TableInfo* inner = swap ? t : r;
        int outer_key = (a.first == outer) ? a.second : b.second;
        int inner_key = (a.first == inner) ? a.second : b.second;

        // Joined output schema: outer columns then inner columns, all qualified.
        // (Bind the qualified schemas to named locals: iterating a reference into a
        // temporary Schema would dangle and silently drop the inner columns.)
        Schema outer_q = Qualify(outer->name, outer->schema);
        Schema inner_q = Qualify(inner->name, inner->schema);
        std::vector<Column> jc = outer_q.Columns();
        for (const auto& c : inner_q.Columns()) jc.push_back(c);
        cur_schema = Schema(jc);

        auto outer_scan = std::make_unique<SeqScanExecutor>(outer, ctx_);
        auto inner_scan = std::make_unique<SeqScanExecutor>(inner, ctx_);
        root = std::make_unique<NestedLoopJoinExecutor>(std::move(outer_scan), std::move(inner_scan),
                                                        outer_key, inner_key, cur_schema);
        explain = "NestedLoopJoin(outer=" + outer->name + " [" + std::to_string(outer->num_rows) +
                  " rows], inner=" + inner->name + " [" + std::to_string(inner->num_rows) + " rows])";

        if (s->where) {
            root = std::make_unique<FilterExecutor>(std::move(root), s->where.get());
            explain += " -> Filter(WHERE)";
        }
    } else {
        ScanPlan sp = Optimizer::ChooseScan(t, s->where.get());
        explain = sp.desc;
        LockMode rm = s->for_update ? LockMode::EXCLUSIVE : LockMode::SHARED;
        if (s->for_update) explain += " [FOR UPDATE: X locks]";
        if (sp.use_index)
            root = std::make_unique<IndexScanExecutor>(t, sp.index, sp.key, ctx_, rm);
        else
            root = std::make_unique<SeqScanExecutor>(t, ctx_, rm);
        cur_schema = t->schema;
        if (s->where) {
            root = std::make_unique<FilterExecutor>(std::move(root), s->where.get());
            explain += " -> Filter(WHERE)";
        }
    }

    // Projection (skip for SELECT *).
    if (!(s->select_list.size() == 1 && s->select_list[0] == "*")) {
        std::vector<int> cols;
        std::vector<Column> out_cols;
        for (const auto& name : s->select_list) {
            int idx = cur_schema.GetColIdx(name);
            if (idx < 0) throw std::runtime_error("unknown column: " + name);
            cols.push_back(idx);
            out_cols.push_back({name, cur_schema.GetColumn(idx).type, false});
        }
        root = std::make_unique<ProjectExecutor>(std::move(root), std::move(cols), Schema(out_cols));
        explain += " -> Project(" + std::to_string(s->select_list.size()) + " cols)";
    }

    return {std::move(root), explain};
}

PhysicalPlan Planner::PlanInsert(InsertStmt* s) {
    TableInfo* t = cat_->GetTable(s->table);
    if (!t) throw std::runtime_error("no such table: " + s->table);
    const Schema& schema = t->schema;

    std::vector<std::vector<Value>> rows;
    for (auto& raw : s->rows) {
        std::vector<Value> full(schema.Count());
        for (size_t i = 0; i < schema.Count(); ++i) full[i] = DefaultValue(schema.GetColumn(i).type);
        if (s->columns.empty()) {
            if (raw.size() != schema.Count())
                throw std::runtime_error("INSERT column count mismatch");
            full = raw;
        } else {
            if (raw.size() != s->columns.size())
                throw std::runtime_error("INSERT value/column count mismatch");
            for (size_t k = 0; k < s->columns.size(); ++k) {
                int idx = schema.GetColIdx(s->columns[k]);
                if (idx < 0) throw std::runtime_error("unknown column: " + s->columns[k]);
                full[idx] = raw[k];
            }
        }
        rows.push_back(std::move(full));
    }
    auto exec = std::make_unique<InsertExecutor>(t, std::move(rows), ctx_);
    return {std::move(exec), "Insert into " + t->name};
}

PhysicalPlan Planner::PlanDelete(DeleteStmt* s) {
    TableInfo* t = cat_->GetTable(s->table);
    if (!t) throw std::runtime_error("no such table: " + s->table);

    ScanPlan sp = Optimizer::ChooseScan(t, s->where.get());
    std::unique_ptr<Executor> scan;
    if (sp.use_index) scan = std::make_unique<IndexScanExecutor>(t, sp.index, sp.key, ctx_);
    else              scan = std::make_unique<SeqScanExecutor>(t, ctx_);
    std::unique_ptr<Executor> child = std::move(scan);
    if (s->where) child = std::make_unique<FilterExecutor>(std::move(child), s->where.get());

    auto exec = std::make_unique<DeleteExecutor>(t, std::move(child), ctx_);
    return {std::move(exec), "Delete via " + sp.desc};
}

}  // namespace minidb
