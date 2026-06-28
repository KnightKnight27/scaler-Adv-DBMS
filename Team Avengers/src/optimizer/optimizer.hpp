// ============================================================================
//  optimizer.hpp — Cost-based query planner.
//
//  Turns a parsed SelectStmt into an executor tree, making two decisions a real
//  optimizer makes:
//
//    1. ACCESS PATH: SeqScan vs IndexScan. If the WHERE clause pins the primary
//       key to a range, an index scan reads only that slice; otherwise a full
//       scan is unavoidable. We estimate the COST of each (in rows touched) and
//       pick the cheaper — this is genuine cost-based selection, not a rule.
//
//    2. JOIN ORDER: for a two-table join we put the SMALLER estimated relation
//       on the inner (buffered) side of the nested loop, since the inner side
//       is materialised once. Fewer buffered tuples = less memory + cheaper
//       rescans.
//
//  Cost is measured in estimated tuples processed. Selectivity (the fraction of
//  rows a predicate keeps) drives the estimate: equality is very selective,
//  ranges less so, AND multiplies, OR adds (capped at 1).
// ============================================================================
#pragma once

#include "../catalog/catalog.hpp"
#include "../execution/operators.hpp"
#include "../sql/ast.hpp"

#include <limits>
#include <memory>
#include <sstream>
#include <string>

namespace minidb {

// A key range [lo, hi] usable by an index scan, plus whether the WHERE actually
// constrained the primary key at all.
struct KeyRange {
    int64_t lo = std::numeric_limits<int64_t>::min();
    int64_t hi = std::numeric_limits<int64_t>::max();
    bool usable = false;   // did we find an AND-chain predicate on the pk?
};

class Optimizer {
public:
    explicit Optimizer(Catalog* cat) : cat_(cat) {}

    // Build the physical plan for a SELECT. `explain` (if non-null) receives a
    // human-readable description of the choices — used by the `EXPLAIN`-style
    // demo output.
    std::unique_ptr<Executor> plan(const SelectStmt& q, std::string* explain = nullptr) {
        std::ostringstream ex;

        if (!q.join.present) {
            auto scan = access_path(cat_->get_table(q.table), q.where.get(), ex);
            auto proj = std::make_unique<ProjectionExecutor>(std::move(scan), q.columns);
            if (explain) *explain = ex.str();
            return proj;
        }

        // --- two-table join ---------------------------------------------------
        TableInfo* a = cat_->get_table(q.table);
        TableInfo* b = cat_->get_table(q.join.table);
        // Join order: smaller relation becomes the inner (buffered) side.
        bool a_inner = a->num_rows <= b->num_rows;
        TableInfo* outer = a_inner ? b : a;
        TableInfo* inner = a_inner ? a : b;
        ex << "JOIN order: outer=" << outer->name << " (" << outer->num_rows
           << " rows), inner=" << inner->name << " (" << inner->num_rows << " rows)\n";

        // For joins we scan both tables unfiltered and apply WHERE after the
        // join (it may reference either table). Access path still cost-chosen.
        auto outer_scan = access_path(outer, nullptr, ex);
        auto inner_scan = access_path(inner, nullptr, ex);
        auto join = std::make_unique<NestedLoopJoinExecutor>(
            std::move(outer_scan), std::move(inner_scan),
            q.join.left_col, q.join.right_col);

        std::unique_ptr<Executor> top = std::move(join);
        if (q.where) top = std::make_unique<FilterExecutor>(std::move(top), q.where.get());
        auto proj = std::make_unique<ProjectionExecutor>(std::move(top), q.columns);
        if (explain) *explain = ex.str();
        return proj;
    }

    // Exposed for testing: estimate the fraction of rows a predicate keeps.
    static double selectivity(const Expr* e) {
        if (!e) return 1.0;
        switch (e->kind) {
            case Expr::Kind::And: return selectivity(e->lhs.get()) * selectivity(e->rhs.get());
            case Expr::Kind::Or: {
                double a = selectivity(e->lhs.get()), b = selectivity(e->rhs.get());
                return std::min(1.0, a + b);              // inclusion-exclusion, simplified
            }
            case Expr::Kind::Compare:
                // Textbook default magic numbers: equality is highly selective,
                // inequalities keep about a third (System R heuristics).
                switch (e->op) {
                    case CmpOp::EQ: return 0.1;
                    case CmpOp::NE: return 0.9;
                    default:        return 0.33;          // <, <=, >, >=
                }
        }
        return 1.0;
    }

private:
    // Choose SeqScan vs IndexScan for one table by comparing estimated cost.
    std::unique_ptr<Executor> access_path(TableInfo* t, const Expr* where,
                                          std::ostringstream& ex) {
        double n = (double)std::max<size_t>(t->num_rows, 1);
        double seq_cost = n;                              // full scan reads every row

        KeyRange kr = extract_pk_range(where, t);
        if (kr.usable) {
            // index cost ~ tree descent + the matched slice
            double matched = n * selectivity(where);
            double idx_cost = (double)t->index->height() + matched;
            if (idx_cost < seq_cost) {
                ex << "ACCESS " << t->name << ": IndexScan on pk in ["
                   << kr.lo << "," << kr.hi << "]  (cost " << (int)idx_cost
                   << " < seq " << (int)seq_cost << ")\n";
                return std::make_unique<IndexScanExecutor>(t, kr.lo, kr.hi, where);
            }
        }
        ex << "ACCESS " << t->name << ": SeqScan  (cost " << (int)seq_cost << ")\n";
        return std::make_unique<SeqScanExecutor>(t, where);
    }

    // Walk an AND-chain of the WHERE clause, narrowing [lo,hi] for the primary
    // key column. OR or predicates on other columns leave the range unusable
    // for that branch (we conservatively fall back to a seq scan then).
    static KeyRange extract_pk_range(const Expr* e, TableInfo* t) {
        KeyRange kr;
        if (!e) return kr;
        const std::string pk = t->schema.columns[t->pk_index].name;
        narrow(e, pk, kr);
        return kr;
    }

    static void narrow(const Expr* e, const std::string& pk, KeyRange& kr) {
        if (e->kind == Expr::Kind::And) {
            narrow(e->lhs.get(), pk, kr);
            narrow(e->rhs.get(), pk, kr);
            return;
        }
        if (e->kind != Expr::Kind::Compare) return;        // OR: don't use index
        // match the predicate's column against the pk (qualified or bare)
        std::string col = e->column;
        auto dot = col.find('.');
        if (dot != std::string::npos) col = col.substr(dot + 1);
        if (col != pk || e->literal.type != ColType::INT) return;

        int64_t k = e->literal.i;
        switch (e->op) {
            case CmpOp::EQ: kr.lo = std::max(kr.lo, k); kr.hi = std::min(kr.hi, k); kr.usable = true; break;
            case CmpOp::GE: kr.lo = std::max(kr.lo, k);     kr.usable = true; break;
            case CmpOp::GT: kr.lo = std::max(kr.lo, k + 1); kr.usable = true; break;
            case CmpOp::LE: kr.hi = std::min(kr.hi, k);     kr.usable = true; break;
            case CmpOp::LT: kr.hi = std::min(kr.hi, k - 1); kr.usable = true; break;
            case CmpOp::NE: break;                          // not a range constraint
        }
    }

    Catalog* cat_;
};

}  // namespace minidb
