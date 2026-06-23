#include "optimizer/optimizer.h"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace minidb {

static Schema ConcatSchema(const Schema &a, const Schema &b) {
  std::vector<Column> cols = a.columns();
  for (const Column &c : b.columns()) cols.push_back(c);
  return Schema(std::move(cols));
}

std::unique_ptr<Executor> Optimizer::BuildScan(const std::string &table,
                                               const std::vector<Predicate> &preds,
                                               std::string *explain) {
  RowStore *store = ctx_->GetStore(table);
  Schema base = store->schema();
  Schema qualified = QualifySchema(base, table);
  int pk = base.pk_index();
  size_t n = store->RowCount();

  // Derive the tightest PK range [low,high] implied by predicates on the PK.
  int64_t low = std::numeric_limits<int64_t>::min();
  int64_t high = std::numeric_limits<int64_t>::max();
  bool has_pk_pred = false;
  if (pk >= 0) {
    for (const Predicate &p : preds) {
      int col = ResolveColumn(qualified, p.column);
      if (col != pk || p.value.type != TypeId::INTEGER) continue;
      has_pk_pred = true;
      int64_t v = p.value.i;
      switch (p.op) {
        case CompareOp::EQ: low = std::max(low, v); high = std::min(high, v); break;
        case CompareOp::GE: low = std::max(low, v); break;
        case CompareOp::GT: low = std::max(low, v == std::numeric_limits<int64_t>::max() ? v : v + 1); break;
        case CompareOp::LE: high = std::min(high, v); break;
        case CompareOp::LT: high = std::min(high, v == std::numeric_limits<int64_t>::min() ? v : v - 1); break;
        case CompareOp::NE: break;  // cannot narrow a range; handled by filter
      }
    }
  }

  std::unique_ptr<Executor> scan;
  bool use_index = false;
  int64_t mn, mx;
  double est_matched = static_cast<double>(n);

  if (has_pk_pred && n > 0 && store->KeyRange(&mn, &mx)) {
    int64_t lo = std::max(low, mn);
    int64_t hi = std::min(high, mx);
    double span = static_cast<double>(mx - mn) + 1.0;
    double frac = (lo > hi) ? 0.0 : (static_cast<double>(hi - lo) + 1.0) / span;
    if (frac < 0.0) frac = 0.0;
    if (frac > 1.0) frac = 1.0;
    est_matched = frac * static_cast<double>(n);

    double seq_cost = static_cast<double>(n);
    double idx_cost = std::log2(static_cast<double>(n) + 1.0) + est_matched;
    use_index = idx_cost < seq_cost;

    if (explain) {
      *explain += "  Table '" + table + "': N=" + std::to_string(n) +
                  ", PK range predicate -> est " +
                  std::to_string(static_cast<long long>(est_matched)) + " rows; " +
                  "seqCost=" + std::to_string(static_cast<long long>(seq_cost)) +
                  " idxCost=" + std::to_string(static_cast<long long>(idx_cost)) +
                  " => " + (use_index ? "IndexScan" : "SeqScan") + "\n";
    }
  } else if (explain) {
    *explain += "  Table '" + table + "': N=" + std::to_string(n) +
                ", no usable PK predicate => SeqScan\n";
  }

  if (use_index) {
    scan = std::make_unique<IndexScanExecutor>(store, qualified, low, high);
  } else {
    scan = std::make_unique<SeqScanExecutor>(store, qualified);
  }

  // The index only narrows; re-check every predicate above the scan so results
  // are always correct (and to handle non-PK / NE predicates).
  if (!preds.empty()) {
    auto bound = BindPredicates(qualified, preds);
    scan = std::make_unique<FilterExecutor>(std::move(scan), std::move(bound));
  }
  return scan;
}

PlanResult Optimizer::PlanSelect(const SelectStmt &stmt) {
  std::string explain = "QUERY PLAN\n";
  std::unique_ptr<Executor> child;

  if (stmt.join.present) {
    RowStore *ls = ctx_->GetStore(stmt.table);
    RowStore *rs = ctx_->GetStore(stmt.join.table);
    Schema lq = QualifySchema(ls->schema(), stmt.table);
    Schema rq = QualifySchema(rs->schema(), stmt.join.table);
    size_t ln = ls->RowCount(), rn = rs->RowCount();

    // Join order: drive the loop with the smaller relation (fewer inner rescans
    // to set up); the inner is rescanned per outer row.
    bool left_outer = (ln <= rn);
    const std::string &outer_tbl = left_outer ? stmt.table : stmt.join.table;
    const std::string &inner_tbl = left_outer ? stmt.join.table : stmt.table;
    RowStore *outer_store = left_outer ? ls : rs;
    RowStore *inner_store = left_outer ? rs : ls;
    Schema outer_sch = left_outer ? lq : rq;
    Schema inner_sch = left_outer ? rq : lq;

    explain += "  Join order: outer='" + outer_tbl + "' (" +
               std::to_string(left_outer ? ln : rn) + " rows), inner='" +
               inner_tbl + "' (" + std::to_string(left_outer ? rn : ln) +
               " rows)  [smaller relation drives]\n";

    auto outer = std::make_unique<SeqScanExecutor>(outer_store, outer_sch);
    auto inner = std::make_unique<SeqScanExecutor>(inner_store, inner_sch);

    // Map the ON columns to outer/inner positions (order-independent).
    int oc, ic;
    try {
      oc = ResolveColumn(outer_sch, stmt.join.left_col);
      ic = ResolveColumn(inner_sch, stmt.join.right_col);
    } catch (const std::exception &) {
      oc = ResolveColumn(outer_sch, stmt.join.right_col);
      ic = ResolveColumn(inner_sch, stmt.join.left_col);
    }

    Schema joined = ConcatSchema(outer_sch, inner_sch);
    child = std::make_unique<NestedLoopJoinExecutor>(std::move(outer), std::move(inner),
                                                     oc, ic, joined);
    explain += "  NestedLoopJoin on " + stmt.join.left_col + " = " + stmt.join.right_col + "\n";

    if (!stmt.where.empty()) {
      auto bound = BindPredicates(joined, stmt.where);
      child = std::make_unique<FilterExecutor>(std::move(child), std::move(bound));
      explain += "  Filter: " + std::to_string(stmt.where.size()) + " predicate(s) after join\n";
    }
  } else {
    child = BuildScan(stmt.table, stmt.where, &explain);
  }

  // Top of the plan: COUNT(*), SELECT *, or a projection.
  std::unique_ptr<Executor> root;
  if (stmt.count_star) {
    root = std::make_unique<CountExecutor>(std::move(child));
    explain += "  Aggregate: COUNT(*)\n";
  } else if (stmt.star) {
    root = std::move(child);
  } else {
    const Schema &cs = child->OutputSchema();
    std::vector<int> idx;
    std::vector<Column> cols;
    for (const std::string &name : stmt.columns) {
      int c = ResolveColumn(cs, name);
      idx.push_back(c);
      cols.push_back(cs.column(c));
    }
    root = std::make_unique<ProjectionExecutor>(std::move(child), std::move(idx),
                                                Schema(std::move(cols)));
    explain += "  Projection: " + std::to_string(stmt.columns.size()) + " column(s)\n";
  }

  return PlanResult{std::move(root), std::move(explain)};
}

}  // namespace minidb
