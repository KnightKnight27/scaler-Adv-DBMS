#include "optimizer/optimizer.hpp"

#include <algorithm>
#include <set>
#include <sstream>

namespace minidb {

namespace {

// Split a predicate tree on top-level AND into a list of conjuncts.
void collect_conjuncts(const ExprPtr& e, std::vector<ExprPtr>& out) {
    if (!e) return;
    if (e->kind == ExprKind::BINARY && e->op == BinOp::AND) {
        collect_conjuncts(e->left, out);
        collect_conjuncts(e->right, out);
    } else {
        out.push_back(e);
    }
}

// Which table aliases does this expression reference?
void expr_aliases(const Expr* e, const std::vector<BaseTable>& tables,
                  std::set<std::string>& out) {
    if (!e) return;
    if (e->kind == ExprKind::COLUMN) {
        if (!e->table.empty()) { out.insert(e->table); return; }
        // Unqualified: find the table whose schema has this column.
        for (auto& bt : tables)
            if (bt.access.info->schema.index_of(e->column) >= 0) { out.insert(bt.alias); return; }
    } else if (e->kind == ExprKind::BINARY) {
        expr_aliases(e->left.get(), tables, out);
        expr_aliases(e->right.get(), tables, out);
    }
}

ExprPtr and_all(const std::vector<ExprPtr>& preds) {
    ExprPtr r;
    for (auto& p : preds) r = r ? Expr::Binary(BinOp::AND, r, p) : p;
    return r;
}

// If `e` is `pkcol OP literal` (or reversed), report op + literal.
bool match_pk_pred(const Expr* e, const std::string& alias, const std::string& pk,
                   BinOp& op, Value& lit) {
    if (!e || e->kind != ExprKind::BINARY) return false;
    auto is_pk_col = [&](const Expr* c) {
        return c->kind == ExprKind::COLUMN && c->column == pk &&
               (c->table.empty() || c->table == alias);
    };
    if (is_pk_col(e->left.get()) && e->right->kind == ExprKind::LITERAL) {
        op = e->op; lit = e->right->literal; return true;
    }
    if (is_pk_col(e->right.get()) && e->left->kind == ExprKind::LITERAL) {
        // flip the operator for `literal OP col`
        switch (e->op) {
            case BinOp::LT: op = BinOp::GT; break;
            case BinOp::LE: op = BinOp::GE; break;
            case BinOp::GT: op = BinOp::LT; break;
            case BinOp::GE: op = BinOp::LE; break;
            default: op = e->op; break;
        }
        lit = e->left->literal; return true;
    }
    return false;
}

}  // namespace

double Optimizer::estimate_cardinality(const BaseTable& bt,
                                       const std::vector<ExprPtr>& local_preds) {
    double card = std::max<double>(1.0, (double)bt.access.info->row_count);
    int pk = bt.access.info->schema.primary_key_index();
    std::string pkname = pk >= 0 ? bt.access.info->schema.column(pk).name : "";
    for (auto& p : local_preds) {
        BinOp op; Value lit;
        bool on_pk = pk >= 0 && match_pk_pred(p.get(), bt.alias, pkname, op, lit);
        double sel;
        if (on_pk && op == BinOp::EQ) sel = card > 0 ? 1.0 / card : 1.0;  // unique key
        else if (p->kind == ExprKind::BINARY && p->op == BinOp::EQ) sel = 0.1;
        else sel = 0.33;   // range / other
        card *= sel;
    }
    return std::max(card, 1.0);
}

OpPtr Optimizer::make_scan(const BaseTable& bt, const std::vector<ExprPtr>& local_preds,
                           std::string& how) {
    int pk = bt.access.info->schema.primary_key_index();
    if (pk >= 0 && bt.access.info->pk_index) {
        std::string pkname = bt.access.info->schema.column(pk).name;
        std::optional<Value> low, high;
        bool usable = false;
        for (auto& p : local_preds) {
            BinOp op; Value lit;
            if (!match_pk_pred(p.get(), bt.alias, pkname, op, lit)) continue;
            usable = true;
            switch (op) {
                case BinOp::EQ: low = lit; high = lit; break;
                case BinOp::GE: low = lit; break;
                case BinOp::GT: low = lit; break;      // inclusive approx; Filter rechecks
                case BinOp::LE: high = lit; break;
                case BinOp::LT: high = lit; break;
                default: break;
            }
        }
        if (usable) {
            how = "IndexScan(pk=" + pkname + ")";
            return std::make_unique<IndexScan>(bt.access, bt.alias, low, high);
        }
    }
    how = "SeqScan";
    return std::make_unique<SeqScan>(bt.access, bt.alias);
}

PhysicalPlan Optimizer::optimize(SelectStmt* stmt, std::vector<BaseTable> tables) {
    std::ostringstream ex;

    // 1. Gather predicates from WHERE and every JOIN ... ON.
    std::vector<ExprPtr> conjuncts;
    collect_conjuncts(stmt->where, conjuncts);
    for (auto& jc : stmt->joins) collect_conjuncts(jc.on, conjuncts);

    // 2. Split into single-table (push-down) vs. multi-table (join) predicates.
    std::vector<std::vector<ExprPtr>> local(tables.size());
    std::vector<std::pair<std::set<std::string>, ExprPtr>> join_preds;
    auto alias_index = [&](const std::string& a) {
        for (size_t i = 0; i < tables.size(); ++i) if (tables[i].alias == a) return (int)i;
        return -1;
    };
    for (auto& c : conjuncts) {
        std::set<std::string> refs;
        expr_aliases(c.get(), tables, refs);
        if (refs.size() <= 1) {
            int ti = refs.empty() ? 0 : alias_index(*refs.begin());
            if (ti >= 0) local[ti].push_back(c);
        } else {
            join_preds.emplace_back(refs, c);
        }
    }

    // 3. Greedy join order: scan most selective (smallest est. card) table first.
    std::vector<int> order(tables.size());
    for (size_t i = 0; i < tables.size(); ++i) order[i] = (int)i;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        return estimate_cardinality(tables[a], local[a]) <
               estimate_cardinality(tables[b], local[b]);
    });

    // 4. Build the left-deep plan.
    ex << "QUERY PLAN\n";
    OpPtr root;
    std::set<std::string> available;
    for (size_t k = 0; k < order.size(); ++k) {
        int ti = order[k];
        const BaseTable& bt = tables[ti];
        std::string how;
        OpPtr scan = make_scan(bt, local[ti], how);
        ex << "  Scan " << bt.access.info->name << " AS " << bt.alias
           << "  [" << how << ", estRows="
           << (long)estimate_cardinality(bt, local[ti]) << "]\n";
        if (!local[ti].empty()) {
            scan = std::make_unique<Filter>(std::move(scan), and_all(local[ti]));
            ex << "    Filter (pushed-down predicate)\n";
        }
        available.insert(bt.alias);

        if (!root) { root = std::move(scan); continue; }

        // Collect now-satisfiable join predicates.
        std::vector<ExprPtr> usable;
        for (auto it = join_preds.begin(); it != join_preds.end();) {
            bool covered = std::all_of(it->first.begin(), it->first.end(),
                [&](const std::string& a) { return available.count(a) > 0; });
            if (covered) { usable.push_back(it->second); it = join_preds.erase(it); }
            else ++it;
        }
        ex << "  NestedLoopJoin" << (usable.empty() ? " (cross)" : " ON predicate") << "\n";
        root = std::make_unique<NestedLoopJoin>(std::move(root), std::move(scan),
                                                and_all(usable));
    }

    // 5. Aggregation (if any aggregate or GROUP BY present).
    bool has_agg = !stmt->group_by.empty();
    for (auto& it : stmt->items) if (it.agg != AggFunc::NONE) has_agg = true;

    if (has_agg) {
        const OutSchema& in = root->schema();
        std::vector<int> group_cols;
        for (auto& [t, c] : stmt->group_by) {
            int idx = resolve_column(in, t, c);
            if (idx < 0) throw std::runtime_error("GROUP BY: unknown column " + c);
            group_cols.push_back(idx);
        }
        std::vector<AggOutput> outs;
        for (auto& it : stmt->items) {
            AggOutput o;
            if (it.agg == AggFunc::NONE) {
                int idx = resolve_column(in, it.table, it.column);
                if (idx < 0) throw std::runtime_error("aggregate query: column " +
                                                      it.column + " must be in GROUP BY");
                // map to its position among group columns
                int gpos = -1;
                for (size_t g = 0; g < group_cols.size(); ++g)
                    if (group_cols[g] == idx) gpos = (int)g;
                if (gpos < 0) throw std::runtime_error("column " + it.column +
                                                       " must appear in GROUP BY");
                o.is_group = true; o.group_index = gpos; o.name = it.alias;
                o.type = in[idx].type;
            } else {
                o.func = it.agg; o.star = it.agg_star; o.name = it.alias;
                o.col_index = it.agg_star ? -1 : resolve_column(in, it.table, it.column);
                o.type = TypeId::INTEGER;
            }
            outs.push_back(o);
        }
        ex << "  Aggregate (" << group_cols.size() << " group cols)\n";
        root = std::make_unique<Aggregate>(std::move(root), group_cols, outs);
    } else {
        root = std::make_unique<Projection>(std::move(root), stmt->items);
        ex << "  Projection\n";
    }

    // 6. ORDER BY.
    if (stmt->has_order_by) {
        int idx = resolve_column(root->schema(), stmt->order_by.table, stmt->order_by.column);
        if (idx < 0) throw std::runtime_error("ORDER BY: unknown column " + stmt->order_by.column);
        ex << "  Sort on " << stmt->order_by.column
           << (stmt->order_by.desc ? " DESC" : " ASC") << "\n";
        root = std::make_unique<Sort>(std::move(root), idx, stmt->order_by.desc);
    }

    return PhysicalPlan{std::move(root), ex.str()};
}

}  // namespace minidb
