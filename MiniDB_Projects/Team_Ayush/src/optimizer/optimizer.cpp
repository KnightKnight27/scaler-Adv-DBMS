#include "optimizer/optimizer.h"

#include <algorithm>
#include <climits>

namespace minidb {

namespace {
bool IsPkColumn(const TableInfo& t, const Predicate& p) {
  if (t.schema.pk_index < 0) return false;
  return p.column == t.schema.columns[t.schema.pk_index].name;
}
}  // namespace

double Optimizer::Selectivity(const TableInfo& t, const Predicate& p) {
  switch (p.op) {
    case CompOp::EQ:
      // Equality on a unique primary key matches at most one row.
      return IsPkColumn(t, p) ? (t.row_count > 0 ? 1.0 / t.row_count : 1.0)
                              : 0.1;  // generic equality heuristic
    case CompOp::NE:
      return 0.9;
    case CompOp::LT:
    case CompOp::LE:
    case CompOp::GT:
    case CompOp::GE:
      return 0.3;  // generic range heuristic
  }
  return 1.0;
}

AccessChoice Optimizer::ChooseAccess(const TableInfo& t,
                                     const std::vector<Predicate>& preds) {
  AccessChoice c;
  const double N = static_cast<double>(std::max(1L, t.row_count));
  c.seq_cost = N;

  // Combine any primary-key predicates into key bounds [low, high].
  bool has_bound = false;
  bool eq = false;
  long low = INT_MIN, high = INT_MAX;
  double sel = 1.0;
  for (const Predicate& p : preds) {
    if (!IsPkColumn(t, p) || p.value.type != ValueType::INT) continue;
    int v = p.value.i;
    switch (p.op) {
      case CompOp::EQ: low = high = v; has_bound = eq = true; sel = std::min(sel, Selectivity(t, p)); break;
      case CompOp::GT: low = std::max(low, (long)v + 1); has_bound = true; sel = std::min(sel, Selectivity(t, p)); break;
      case CompOp::GE: low = std::max(low, (long)v); has_bound = true; sel = std::min(sel, Selectivity(t, p)); break;
      case CompOp::LT: high = std::min(high, (long)v - 1); has_bound = true; sel = std::min(sel, Selectivity(t, p)); break;
      case CompOp::LE: high = std::min(high, (long)v); has_bound = true; sel = std::min(sel, Selectivity(t, p)); break;
      case CompOp::NE: break;  // not usable as an index bound
    }
  }

  const bool indexed = (t.pk_index_header != INVALID_PAGE_ID);
  if (indexed && has_bound) {
    double est = eq ? 1.0 : sel * N;
    const double height = 3.0;  // approximate B+Tree traversal cost
    c.index_cost = est + height;
    if (c.index_cost < c.seq_cost) {
      c.use_index = true;
      c.low = static_cast<int32_t>(std::max<long>(low, INT_MIN));
      c.high = static_cast<int32_t>(std::min<long>(high, INT_MAX));
      c.est_rows = est;
      c.reason = "IndexScan on PK (est_rows=" + std::to_string((long)est) +
                 ", index_cost=" + std::to_string((long)c.index_cost) +
                 " < seq_cost=" + std::to_string((long)c.seq_cost) + ")";
      return c;
    }
  }

  // Fall back to a sequential scan; estimate output via remaining selectivity.
  double s = 1.0;
  for (const Predicate& p : preds) s *= Selectivity(t, p);
  c.use_index = false;
  c.est_rows = s * N;
  c.reason = indexed
                 ? "SeqScan (no usable/selective PK bound; seq_cost=" +
                       std::to_string((long)c.seq_cost) + ")"
                 : "SeqScan (no PK index; seq_cost=" +
                       std::to_string((long)c.seq_cost) + ")";
  return c;
}

}  // namespace minidb
