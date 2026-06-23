#include "minidb/exec/planner.hpp"

#include "minidb/exec/predicate.hpp"
#include <stdexcept>

namespace minidb {

size_t Planner::row_estimate(Catalog& c, StorageEngine& e, TableId t) {
  TableStats& st = c.stats(t);
  if (st.row_count == 0) {
    auto it = e.scan(t);
    RID rid;
    Tuple tup;
    size_t n = 0;
    while (it->next(rid, tup)) ++n;
    st.row_count = n;
  }
  return st.row_count;
}

double Planner::eq_selectivity(Catalog& c, TableId t, size_t col) {
  TableStats& st = c.stats(t);
  auto it = st.distinct.find(col);
  if (it != st.distinct.end() && it->second > 0) return 1.0 / static_cast<double>(it->second);
  return 0.1;  // default when un-analyzed
}

std::unique_ptr<Operator> Planner::build_internal(const SelectStmt& s, Catalog& catalog,
                                                  StorageEngine& engine, std::string* trace) {
  TableMeta& left_meta = catalog.by_name(s.table);
  std::unique_ptr<Operator> root;

  if (s.join.present) {
    TableMeta& right_meta = catalog.by_name(s.join.table);

    std::vector<Column> jcols;
    for (Column c : left_meta.schema.columns()) {
      c.name = left_meta.name + "." + c.name;
      jcols.push_back(c);
    }
    for (Column c : right_meta.schema.columns()) {
      c.name = right_meta.name + "." + c.name;
      jcols.push_back(c);
    }
    Schema joined(std::move(jcols));

    auto belongs_left = [&](const std::string& ref) {
      std::string q = table_qual(ref);
      if (!q.empty()) return q == left_meta.name;
      return left_meta.schema.index_of(bare_name(ref)) != Schema::npos;
    };
    const std::string& left_ref =
        belongs_left(s.join.left_col) ? s.join.left_col : s.join.right_col;
    const std::string& right_ref =
        belongs_left(s.join.left_col) ? s.join.right_col : s.join.left_col;
    size_t lk = left_meta.schema.index_of(bare_name(left_ref));
    size_t rk = right_meta.schema.index_of(bare_name(right_ref));
    if (lk == Schema::npos || rk == Schema::npos)
      throw std::runtime_error("unknown join column");

    size_t lrows = row_estimate(catalog, engine, left_meta.id);
    size_t rrows = row_estimate(catalog, engine, right_meta.id);
    double hash_cost = static_cast<double>(lrows) + static_cast<double>(rrows);
    double nl_cost = static_cast<double>(lrows) * static_cast<double>(rrows);
    bool use_hash = hash_cost <= nl_cost;

    auto left_scan = std::unique_ptr<Operator>(new SeqScanOp(engine, left_meta.id, left_meta.schema));
    auto right_scan = std::unique_ptr<Operator>(new SeqScanOp(engine, right_meta.id, right_meta.schema));
    if (use_hash)
      root.reset(new HashJoinOp(std::move(left_scan), std::move(right_scan), lk, rk, joined));
    else
      root.reset(new NestedLoopJoinOp(std::move(left_scan), std::move(right_scan), lk, rk, joined));

    if (trace) {
      *trace += (use_hash ? "HashJoin " : "NestedLoopJoin ");
      *trace += left_meta.name + " x " + right_meta.name + "  (left~" +
                std::to_string(lrows) + ", right~" + std::to_string(rrows) +
                ", hash_cost=" + std::to_string(hash_cost) +
                ", nl_cost=" + std::to_string(nl_cost) + ")\n";
      *trace += "    SeqScan " + left_meta.name + "\n";
      *trace += "    SeqScan " + right_meta.name + "\n";
    }
  } else {
    size_t rows = row_estimate(catalog, engine, left_meta.id);
    bool used_index = false;
    for (const auto& p : s.where) {
      if (p.op != CmpOp::Eq) continue;
      size_t col = left_meta.schema.index_of(bare_name(p.column));
      if (col == Schema::npos) continue;
      bool has_index = false;
      for (const IndexMeta* ix : catalog.indexes_for(left_meta.id))
        if (ix->column == col) { has_index = true; break; }
      if (!has_index) continue;
      double sel = eq_selectivity(catalog, left_meta.id, col);
      double idx_cost = sel * static_cast<double>(rows) + 1.0;
      double seq_cost = static_cast<double>(rows);
      if (idx_cost < seq_cost) {
        root.reset(new IndexScanOp(engine, left_meta.id, col, p.value, left_meta.schema));
        used_index = true;
        if (trace)
          *trace += "IndexScan " + left_meta.name + "." +
                    left_meta.schema.column(col).name + " = " + p.value.to_string() +
                    "  (rows~" + std::to_string(rows) + ", sel=" + std::to_string(sel) +
                    ", idx_cost=" + std::to_string(idx_cost) + ", seq_cost=" +
                    std::to_string(seq_cost) + ")\n";
        break;
      }
    }
    if (!used_index) {
      root.reset(new SeqScanOp(engine, left_meta.id, left_meta.schema));
      if (trace) *trace += "SeqScan " + left_meta.name + "  (rows~" + std::to_string(rows) + ")\n";
    }
  }

  if (!s.where.empty()) {
    root.reset(new FilterOp(std::move(root), s.where));
    if (trace) *trace += "Filter [" + std::to_string(s.where.size()) + " predicate(s)]\n";
  }

  if (!s.columns.empty()) {
    const Schema& cur = root->out_schema();
    std::vector<size_t> indices;
    std::vector<Column> out_cols;
    for (const auto& name : s.columns) {
      size_t idx = resolve_column(cur, name);
      if (idx == Schema::npos) throw std::runtime_error("unknown column: " + name);
      indices.push_back(idx);
      out_cols.push_back(cur.column(idx));
    }
    root.reset(new ProjectOp(std::move(root), std::move(indices), Schema(std::move(out_cols))));
    if (trace) *trace += "Project [" + std::to_string(s.columns.size()) + " column(s)]\n";
  }

  if (!s.order_by.empty()) {
    std::vector<size_t> keys;
    for (const auto& name : s.order_by) {
      size_t idx = resolve_column(root->out_schema(), name);
      if (idx == Schema::npos) throw std::runtime_error("ORDER BY unknown column: " + name);
      keys.push_back(idx);
    }
    root.reset(new SortOp(std::move(root), std::move(keys)));
    if (trace) *trace += "Sort\n";
  }

  return root;
}

std::unique_ptr<Operator> Planner::build(const SelectStmt& s, Catalog& catalog,
                                         StorageEngine& engine) {
  return build_internal(s, catalog, engine, nullptr);
}

std::string Planner::explain(const SelectStmt& s, Catalog& catalog, StorageEngine& engine) {
  std::string trace = "Query plan (bottom-up):\n";
  build_internal(s, catalog, engine, &trace);
  return trace;
}

}  // namespace minidb
