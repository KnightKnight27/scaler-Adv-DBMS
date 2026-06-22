#include "exec/executor.h"
#include <stdexcept>

namespace minidb {
namespace {

// Resolve a column reference against a row layout. Qualified refs match
// table+name; unqualified refs match the first column with that name.
int resolve(const std::vector<ColMeta> &cols, const ColumnRef &ref) {
    for (size_t i = 0; i < cols.size(); ++i) {
        if (!ref.table.empty() && cols[i].table != ref.table) continue;
        if (cols[i].name == ref.column) return (int)i;
    }
    throw std::runtime_error("unknown column: " +
        (ref.table.empty() ? ref.column : ref.table + "." + ref.column));
}

Value eval_operand(const Operand &o, const std::vector<ColMeta> &cols, const Tuple &row) {
    if (!o.is_column) return o.constant;
    return row[resolve(cols, o.col)];
}

bool eval_compare(const Value &a, CompOp op, const Value &b) {
    try {
        int c = a.compare(b);
        switch (op) {
            case CompOp::EQ: return c == 0;
            case CompOp::NE: return c != 0;
            case CompOp::LT: return c < 0;
            case CompOp::LE: return c <= 0;
            case CompOp::GT: return c > 0;
            case CompOp::GE: return c >= 0;
        }
    } catch (...) { return false; } // incomparable types never match
    return false;
}

bool eval_pred(const Predicate &p, const std::vector<ColMeta> &cols, const Tuple &row) {
    Value l = eval_operand(p.left, cols, row);
    Value r = eval_operand(p.right, cols, row);
    return eval_compare(l, p.op, r);
}

} // namespace

RowSet Executor::scan_table(const TablePlan &plan) {
    TableInfo *ti = db_.catalog().get_table(plan.table);
    if (!ti) throw std::runtime_error("no such table: " + plan.table);

    // Reads take a shared lock on the table (strict 2PL, serializable).
    db_.locks().acquire(txn_, LockManager::table_resource(plan.table), LockMode::SHARED);

    RowSet rs;
    for (const auto &c : ti->schema.columns)
        rs.cols.push_back({plan.table, c.name, c.type});

    auto keep = [&](const Tuple &row) {
        for (const auto &p : plan.filters)
            if (!eval_pred(p, rs.cols, row)) return false;
        return true;
    };

    if (plan.method == AccessMethod::INDEX_POINT) {
        // Primary-key point lookup: probe the B+Tree, fetch the one row.
        RID rid;
        if (ti->index->search(plan.point_key, &rid)) {
            Tuple row;
            if (ti->heap->get_tuple(rid, &row) && keep(row)) {
                rs.rows.push_back(std::move(row));
                rs.rids.push_back(rid);
            }
        }
    } else {
        // Sequential scan over the heap.
        for (auto &rt : ti->heap->scan()) {
            if (keep(rt.second)) {
                rs.rows.push_back(std::move(rt.second));
                rs.rids.push_back(rt.first);
            }
        }
    }
    return rs;
}

QueryResult Executor::project(const SelectStmt &stmt, const RowSet &rs) {
    QueryResult out;
    out.ok = true;

    std::vector<int> proj;
    if (stmt.select_star) {
        for (size_t i = 0; i < rs.cols.size(); ++i) proj.push_back((int)i);
    } else {
        for (const auto &cref : stmt.columns) proj.push_back(resolve(rs.cols, cref));
    }
    for (int idx : proj) {
        const ColMeta &c = rs.cols[idx];
        // Qualify headers in a join so duplicate names stay distinct.
        out.columns.push_back(c.table.empty() ? c.name : c.table + "." + c.name);
    }
    for (const auto &row : rs.rows) {
        Tuple r;
        for (int idx : proj) r.push_back(row[idx]);
        out.rows.push_back(std::move(r));
    }
    out.message = std::to_string(out.rows.size()) + " row(s)";
    return out;
}

QueryResult Executor::run_select(const SelectStmt &stmt) {
    Optimizer opt(db_.catalog());
    QueryPlan plan = opt.plan_select(stmt);

    if (!plan.has_join) {
        RowSet rs = scan_table(plan.outer);
        QueryResult res = project(stmt, rs);
        res.message += "  [plan: " + plan.explanation + "]";
        return res;
    }

    // ---- Nested-loop join ----
    RowSet outer = scan_table(plan.outer);
    RowSet inner = scan_table(plan.inner);

    RowSet joined;
    joined.cols = outer.cols;
    joined.cols.insert(joined.cols.end(), inner.cols.begin(), inner.cols.end());

    int lo = resolve(outer.cols, plan.join_pred.left.col.table == plan.outer.table
                                 ? plan.join_pred.left.col : plan.join_pred.right.col);
    int ri = resolve(inner.cols, plan.join_pred.left.col.table == plan.inner.table
                                 ? plan.join_pred.left.col : plan.join_pred.right.col);

    for (const auto &orow : outer.rows) {
        for (const auto &irow : inner.rows) {
            if (!eval_compare(orow[lo], CompOp::EQ, irow[ri])) continue;
            Tuple combined = orow;
            combined.insert(combined.end(), irow.begin(), irow.end());
            joined.rows.push_back(std::move(combined));
        }
    }
    QueryResult res = project(stmt, joined);
    res.message += "  [plan: " + plan.explanation + "]";
    return res;
}

QueryResult Executor::run_insert(const InsertStmt &stmt) {
    TableInfo *ti = db_.catalog().get_table(stmt.table);
    if (!ti) return QueryResult::error("no such table: " + stmt.table);
    if ((int)stmt.values.size() != ti->schema.column_count())
        return QueryResult::error("column count mismatch in INSERT");

    // Writes take an exclusive table lock.
    db_.locks().acquire(txn_, LockManager::table_resource(stmt.table), LockMode::EXCLUSIVE);

    // Enforce primary-key uniqueness via the index.
    if (ti->has_index) {
        int pk = ti->schema.pk_index;
        RID existing;
        if (ti->index->search(stmt.values[pk].as_int(), &existing))
            return QueryResult::error("duplicate primary key");
    }
    db_.insert_row(ti, stmt.values, txn_, /*logging=*/true);
    return QueryResult::status("INSERT 1");
}

QueryResult Executor::run_delete(const DeleteStmt &stmt) {
    TableInfo *ti = db_.catalog().get_table(stmt.table);
    if (!ti) return QueryResult::error("no such table: " + stmt.table);

    db_.locks().acquire(txn_, LockManager::table_resource(stmt.table), LockMode::EXCLUSIVE);

    // Build a column layout to evaluate the WHERE clause.
    std::vector<ColMeta> cols;
    for (const auto &c : ti->schema.columns) cols.push_back({stmt.table, c.name, c.type});

    int deleted = 0;
    for (auto &rt : ti->heap->scan()) {
        bool match = true;
        for (const auto &p : stmt.where)
            if (!eval_pred(p, cols, rt.second)) { match = false; break; }
        if (!match) continue;
        db_.delete_row(ti, rt.first, rt.second, txn_, /*logging=*/true);
        ++deleted;
    }
    return QueryResult::status("DELETE " + std::to_string(deleted));
}

} // namespace minidb
