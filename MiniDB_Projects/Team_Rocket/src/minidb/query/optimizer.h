#pragma once

#include <algorithm>
#include <memory>
#include <string>

#include "exec_context.h"
#include "operators.h"
#include "parser.h"

namespace minidb {

struct Plan {
    std::unique_ptr<Operator> root;
    std::string explain;
};

// A textbook cost-based optimizer: it estimates how many rows a predicate lets
// through (selectivity), uses that to pick the cheaper join order, and chooses
// an index scan over a sequential scan when an equality hits the indexed column.
class Optimizer {
public:
    static Plan build(ExecContext* ctx, const Statement& st) {
        Plan plan;
        std::string ex;
        std::unique_ptr<Operator> root;

        if (!st.has_join) {
            root = scan_for(ctx, st.table, st.where, ex);
        } else {
            double card_a = est_card(ctx, st.table, st.where);
            double card_b = est_card(ctx, st.join_table, st.where);
            const std::string& outer = card_a <= card_b ? st.table : st.join_table;
            const std::string& inner = card_a <= card_b ? st.join_table : st.table;
            ex += "join order: outer=" + outer + " (~" +
                  std::to_string(static_cast<long long>(std::min(card_a, card_b))) +
                  " rows), inner=" + inner + "\n";
            auto outer_op = scan_for(ctx, outer, st.where, ex);
            auto inner_op = scan_for(ctx, inner, st.where, ex);
            root = std::make_unique<NestedLoopJoin>(std::move(outer_op), std::move(inner_op),
                                                    st.left_on, st.right_on);
            ex += "nested-loop join on " + st.left_on + " = " + st.right_on + "\n";
        }

        if (!st.select_cols.empty() &&
            !(st.select_cols.size() == 1 && st.select_cols[0] == "*")) {
            root = std::make_unique<Project>(std::move(root), st.select_cols);
            ex += "project " + std::to_string(st.select_cols.size()) + " column(s)\n";
        }

        plan.root = std::move(root);
        plan.explain = ex;
        return plan;
    }

private:
    static double selectivity(const Predicate& p) {
        if (!p.present) return 1.0;
        if (p.op == "=") return 0.1;
        if (p.op == "!=") return 0.9;
        return 0.33;  // range predicate
    }

    static bool belongs(const std::string& col, const std::string& table, const TableInfo& ti) {
        auto dot = col.find('.');
        if (dot != std::string::npos) return col.substr(0, dot) == table;
        return column_index(ti.schema, col) >= 0;
    }

    static double est_card(ExecContext* ctx, const std::string& table, const Predicate& w) {
        TableInfo& ti = ctx->cat->get(table);
        double n = static_cast<double>(ti.num_tuples);
        if (w.present && belongs(w.col, table, ti)) n *= selectivity(w);
        return n;
    }

    static std::unique_ptr<Operator> scan_for(ExecContext* ctx, const std::string& table,
                                              const Predicate& w, std::string& ex) {
        TableInfo& ti = ctx->cat->get(table);
        bool applies = w.present && belongs(w.col, table, ti);
        if (applies && w.op == "=" && w.val.type == Type::Int &&
            column_index(ti.schema, w.col) == ti.index_col && ti.index_col >= 0) {
            ex += "index scan on " + table + " (key " + std::to_string(w.val.i) +
                  ", selectivity 0.10)\n";
            return std::make_unique<IndexScan>(ctx, table, w.val.i);
        }
        ex += "seq scan on " + table;
        std::unique_ptr<Operator> op = std::make_unique<SeqScan>(ctx, table);
        if (applies) {
            ex += " + filter (selectivity " + sel_str(w) + ")";
            op = std::make_unique<Filter>(std::move(op), w);
        }
        ex += "\n";
        return op;
    }

    static std::string sel_str(const Predicate& w) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.2f", selectivity(w));
        return buf;
    }
};

}  // namespace minidb
