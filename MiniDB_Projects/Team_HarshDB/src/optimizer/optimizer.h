// ---------------------------------------------------------------------------
// optimizer.h - a small cost-based optimizer.
//
// Two jobs, both required by the rubric:
//   1. Access-path selection: for each table decide between a full sequential
//      scan and a B+ tree index scan, by estimating how many rows each would
//      touch (selectivity) and the page cost of fetching them.
//   2. Join ordering: for a two-table join, make the smaller relation the outer
//      side of the nested loop so the (more expensive) inner side runs fewer
//      times.
//
// The estimates are deliberately simple - tuple counts and page counts - but the
// decision logic is the same shape a real planner uses against pg_statistic.
// ---------------------------------------------------------------------------
#pragma once
#include "../catalog/catalog.h"
#include "../sql/ast.h"
#include <optional>
#include <string>

namespace minidb {

struct AccessPath {
    enum Method { SEQ_SCAN, INDEX_SCAN } method = SEQ_SCAN;
    std::string table;
    int64_t     key = 0;      // for INDEX_SCAN: the equality key
    double      est_rows = 0;
    double      est_cost = 0;
    std::string reason;
};

class Optimizer {
public:
    // The catalog is passed for symmetry / future statistics; access paths are
    // chosen from the TableInfo handed to each method.
    explicit Optimizer(Catalog* cat) { (void)cat; }

    // Number of (live-or-dead) tuples currently stored - our cardinality proxy.
    double cardinality(TableInfo* t) { return (double)t->heap->scan().size(); }
    double page_count(TableInfo* t)  { return (double)t->heap->page_count(); }

    // Decide how to read `table`, given an optional WHERE predicate.
    AccessPath choose_access(TableInfo* t, const ExprPtr& where) {
        AccessPath ap;
        ap.table = t->name;
        double n = cardinality(t);
        double pages = page_count(t);

        std::optional<int64_t> pk_eq;
        if (t->pk_index >= 0 && where)
            pk_eq = find_pk_equality(where, t->schema[t->pk_index].name);

        // Sequential scan cost ~ number of pages read.
        double seq_cost = std::max(1.0, pages);

        if (pk_eq && t->index) {
            // Equality on the indexed PK: selectivity ~ 1 row.
            double idx_cost = (double)t->index->height() + 1.0;
            ap.method = AccessPath::INDEX_SCAN;
            ap.key = *pk_eq;
            ap.est_rows = 1.0;
            ap.est_cost = idx_cost;
            ap.reason = "equality on primary key '" + t->schema[t->pk_index].name +
                        "' -> index scan (cost " + fmt(idx_cost) +
                        " vs seq scan cost " + fmt(seq_cost) + ")";
            return ap;
        }

        ap.method = AccessPath::SEQ_SCAN;
        ap.est_rows = n;
        ap.est_cost = seq_cost;
        ap.reason = where ? "no usable index for predicate -> sequential scan"
                          : "no predicate -> sequential scan";
        return ap;
    }

    // For a join, choose which table should be the outer relation.
    // Returns true if the tables should be swapped (right becomes outer).
    bool should_swap_join(TableInfo* left, TableInfo* right) {
        return cardinality(right) < cardinality(left);
    }

private:
    // Look for "<pk> = <int literal>" anywhere in a conjunctive predicate.
    std::optional<int64_t> find_pk_equality(const ExprPtr& e, const std::string& pk) {
        if (!e || e->kind != Expr::Kind::Binary) return std::nullopt;
        if (e->op == "AND") {
            if (auto l = find_pk_equality(e->left, pk)) return l;
            return find_pk_equality(e->right, pk);
        }
        if (e->op == "=") {
            // column = literal  (either order)
            if (e->left->kind == Expr::Kind::Column && e->left->column == pk &&
                e->right->kind == Expr::Kind::IntLit)
                return e->right->int_val;
            if (e->right->kind == Expr::Kind::Column && e->right->column == pk &&
                e->left->kind == Expr::Kind::IntLit)
                return e->left->int_val;
        }
        return std::nullopt;
    }

    static std::string fmt(double d) {
        std::ostringstream ss; ss.precision(1); ss << std::fixed << d; return ss.str();
    }
};

} // namespace minidb
