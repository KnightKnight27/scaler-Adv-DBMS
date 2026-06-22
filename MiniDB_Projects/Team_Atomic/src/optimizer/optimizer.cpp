#include "optimizer/optimizer.h"
#include "record/tuple.h"
#include <algorithm>
#include <limits>
#include <cmath>

namespace minidb {

TableStats Optimizer::Analyze(TableMeta* meta) {
  TableStats st;
  st.has_pk_index = meta->HasKeyAccess();
  int64_t mn = std::numeric_limits<int64_t>::max();
  int64_t mx = std::numeric_limits<int64_t>::min();
  int pk = meta->schema.PkIndex();
  auto cursor = meta->store->FullScan();
  std::string bytes;
  while (cursor->Next(&bytes)) {
    st.rows++;
    if (pk >= 0) {
      Tuple t = Tuple::Deserialize(bytes.data(), meta->schema);
      int64_t k = t.GetValue(pk).i;
      mn = std::min(mn, k);
      mx = std::max(mx, k);
    }
  }
  st.pk_min = (st.rows ? mn : 0);
  st.pk_max = (st.rows ? mx : 0);
  return st;
}

double Optimizer::SelectivityRows(int64_t low, int64_t high, const TableStats& st) {
  if (st.rows == 0) return 0;
  if (low > high) return 0;
  int64_t span = st.pk_max - st.pk_min + 1;
  if (span <= 0) span = 1;
  int64_t want = high - low + 1;
  double frac = static_cast<double>(want) / static_cast<double>(span);
  frac = std::min(1.0, std::max(0.0, frac));
  double est = frac * static_cast<double>(st.rows);
  return std::max(1.0, est);  // at least one row if the range is non-empty
}

bool Optimizer::ExtractPkRange(const Statement& s, TableMeta* meta, int64_t* low,
                               int64_t* high, std::vector<bool>* consumed) {
  consumed->assign(s.where.size(), false);
  if (!meta->HasKeyAccess()) return false;
  int pk = meta->schema.PkIndex();
  const std::string& pk_name = meta->schema.GetColumn(pk).name;

  int64_t lo = std::numeric_limits<int64_t>::min();
  int64_t hi = std::numeric_limits<int64_t>::max();
  bool any = false;

  for (size_t i = 0; i < s.where.size(); i++) {
    const Predicate& p = s.where[i];
    // Only literal comparisons on the PK column are index-usable.
    if (p.rhs_is_col || p.lhs.column != pk_name) continue;
    if (!p.lhs.table.empty() && p.lhs.table != s.table) continue;
    if (p.rhs_val.type != TypeId::INTEGER) continue;
    int64_t c = p.rhs_val.i;
    switch (p.op) {
      case CmpOp::EQ: lo = std::max(lo, c); hi = std::min(hi, c); break;
      case CmpOp::GT: if (c < std::numeric_limits<int64_t>::max()) lo = std::max(lo, c + 1); break;
      case CmpOp::GE: lo = std::max(lo, c); break;
      case CmpOp::LT: if (c > std::numeric_limits<int64_t>::min()) hi = std::min(hi, c - 1); break;
      case CmpOp::LE: hi = std::min(hi, c); break;
      case CmpOp::NE: continue;  // not a contiguous range; leave as filter
    }
    consumed->at(i) = true;
    any = true;
  }
  if (!any) return false;
  *low = lo;
  *high = hi;
  return true;
}

static std::unique_ptr<Executor> SeqScan(TableMeta* m) {
  return std::make_unique<SeqScanExecutor>(m->store.get(), m->schema, m->name);
}

PlanResult Optimizer::PlanSelect(const Statement& s) {
  TableMeta* meta = catalog_->GetTable(s.table);
  if (!meta) throw DBError("no such table: " + s.table);

  PlanResult pr;

  // ---- JOIN: pick the cheaper outer/inner ordering ----
  if (s.join.present) {
    TableMeta* rmeta = catalog_->GetTable(s.join.table);
    if (!rmeta) throw DBError("no such table: " + s.join.table);
    TableStats ls = Analyze(meta), rs = Analyze(rmeta);
    // Nested-loop cost ~ |outer| + |outer| * |inner|; smaller outer wins.
    double cost_lr = ls.rows + ls.rows * rs.rows;       // FROM-table outer
    double cost_rl = rs.rows + rs.rows * ls.rows;       // JOIN-table outer
    std::unique_ptr<Executor> outer, inner;
    std::string desc;
    if (cost_rl < cost_lr) {
      outer = SeqScan(rmeta); inner = SeqScan(meta);
      desc = "NestedLoopJoin[outer=" + rmeta->name + "(" + std::to_string(rs.rows) +
             "), inner=" + meta->name + "(" + std::to_string(ls.rows) +
             ")] est_cost=" + std::to_string((long long)cost_rl);
    } else {
      outer = SeqScan(meta); inner = SeqScan(rmeta);
      desc = "NestedLoopJoin[outer=" + meta->name + "(" + std::to_string(ls.rows) +
             "), inner=" + rmeta->name + "(" + std::to_string(rs.rows) +
             ")] est_cost=" + std::to_string((long long)cost_lr);
    }
    std::unique_ptr<Executor> node = std::make_unique<NestedLoopJoinExecutor>(
        std::move(outer), std::move(inner), s.join.on);

    if (!s.where.empty()) {
      std::vector<BoundPredicate> preds;
      for (auto& p : s.where) preds.push_back(BindPredicate(node->GetSchema(), p));
      node = std::make_unique<FilterExecutor>(std::move(node), std::move(preds));
      desc = "Filter(" + desc + ")";
    }
    if (s.count_star) { node = std::make_unique<CountExecutor>(std::move(node)); desc = "Count(" + desc + ")"; }
    else if (!s.select_star) { node = std::make_unique<ProjectionExecutor>(std::move(node), s.select_columns); desc = "Project(" + desc + ")"; }
    pr.root = std::move(node);
    pr.description = desc;
    return pr;
  }

  // ---- Single table: choose seq scan vs index scan ----
  TableStats st = Analyze(meta);
  int64_t low, high;
  std::vector<bool> consumed;
  bool usable = ExtractPkRange(s, meta, &low, &high, &consumed);

  std::unique_ptr<Executor> node;
  std::string desc;
  bool use_index = false;
  if (usable) {
    double est = SelectivityRows(low, high, st);
    double seq_cost = static_cast<double>(st.rows) + 1.0;
    double idx_cost = std::log2(std::max<int64_t>(2, st.rows)) + est;  // descent + matches
    use_index = idx_cost < seq_cost;
    char buf[256];
    if (use_index) {
      std::snprintf(buf, sizeof(buf),
                    "IndexScan(%s, pk in [%lld,%lld]) est_rows=%.0f idx_cost=%.1f < seq_cost=%.1f",
                    meta->name.c_str(), (long long)low, (long long)high, est, idx_cost, seq_cost);
    } else {
      std::snprintf(buf, sizeof(buf),
                    "SeqScan(%s) rows=%lld (idx_cost=%.1f >= seq_cost=%.1f, range not selective)",
                    meta->name.c_str(), (long long)st.rows, idx_cost, seq_cost);
    }
    desc = buf;
  } else {
    desc = "SeqScan(" + meta->name + ") rows=" + std::to_string(st.rows) +
           (st.has_pk_index ? " (no usable PK predicate)" : " (no index)");
  }

  if (use_index) {
    node = std::make_unique<IndexScanExecutor>(meta->store.get(), meta->schema,
                                               meta->name, low, high);
  } else {
    node = SeqScan(meta);
  }

  // Residual predicates: everything not absorbed by an index range.
  std::vector<Predicate> residual;
  for (size_t i = 0; i < s.where.size(); i++)
    if (!(use_index && i < consumed.size() && consumed[i])) residual.push_back(s.where[i]);
  if (!residual.empty()) {
    std::vector<BoundPredicate> preds;
    for (auto& p : residual) preds.push_back(BindPredicate(node->GetSchema(), p));
    node = std::make_unique<FilterExecutor>(std::move(node), std::move(preds));
    desc = "Filter(" + desc + ")";
  }

  if (s.count_star) { node = std::make_unique<CountExecutor>(std::move(node)); desc = "Count(" + desc + ")"; }
  else if (!s.select_star) { node = std::make_unique<ProjectionExecutor>(std::move(node), s.select_columns); desc = "Project(" + desc + ")"; }

  pr.root = std::move(node);
  pr.description = desc;
  return pr;
}

}  // namespace minidb
