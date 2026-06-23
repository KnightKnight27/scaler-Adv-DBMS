#include "plan/optimizer.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>

namespace minidb {
namespace {

// A key range the index can scan; `point` marks an equality lookup.
struct KeyRange {
  Key  lo;
  Key  hi;
  bool point;
};

KeyRange intersect(const KeyRange& a, const KeyRange& b) {
  Key lo = std::max(a.lo, b.lo);
  Key hi = std::min(a.hi, b.hi);
  return {lo, hi, lo == hi};
}

// Looks for a predicate on the primary key that the B+Tree can scan as a range.
// Returns nullopt when no index-usable bound exists (then we full-scan).
std::optional<KeyRange> analyze_pk_range(const Expr* expr, const std::string& pk,
                                         Key min_key, Key max_key) {
  const auto* bin = dynamic_cast<const BinaryExpr*>(expr);
  if (!bin) return std::nullopt;

  if (bin->op == "AND") {
    auto l = analyze_pk_range(bin->left.get(), pk, min_key, max_key);
    auto r = analyze_pk_range(bin->right.get(), pk, min_key, max_key);
    if (l && r) return intersect(*l, *r);
    return l ? l : r;  // the other half is still applied by the Filter
  }
  if (bin->op == "OR") return std::nullopt;  // an index range could miss rows

  // A comparison: require <pk-column> <op> <int-literal>.
  const auto* col = dynamic_cast<const ColumnRef*>(bin->left.get());
  const auto* lit = dynamic_cast<const IntLit*>(bin->right.get());
  if (!col || !lit || col->column != pk) return std::nullopt;
  Key v = lit->value;
  if (bin->op == "=")  return KeyRange{v, v, true};
  if (bin->op == "<")  return KeyRange{min_key, v - 1, false};
  if (bin->op == "<=") return KeyRange{min_key, v, false};
  if (bin->op == ">")  return KeyRange{v + 1, max_key, false};
  if (bin->op == ">=") return KeyRange{v, max_key, false};
  return std::nullopt;
}

}  // namespace

OperatorPtr Optimizer::build_scan(TableInfo& table, const Expr* where) {
  Schema schema = qualified_schema(table);
  const TableStats& stats = table.storage->stats();
  const std::string& pk = table.schema.columns[table.pk_col].name;

  auto range = where ? analyze_pk_range(where, pk, stats.min_key, stats.max_key) : std::nullopt;
  if (range && table.storage->supports_index_scan() && stats.row_count > 0) {
    double rows = static_cast<double>(stats.row_count);
    double span = static_cast<double>(stats.max_key - stats.min_key + 1);
    double selectivity = range->point ? 1.0 / rows
                                       : static_cast<double>(range->hi - range->lo + 1) /
                                             std::max(1.0, span);
    selectivity = std::clamp(selectivity, 0.0, 1.0);
    double cost_seq   = rows;                                 // read every row
    double cost_index = selectivity * rows + std::log2(rows + 1);
    if (cost_index < cost_seq)
      return std::make_unique<IndexScan>(*table.storage, schema, table.name, range->lo, range->hi);
  }
  return std::make_unique<SeqScan>(*table.storage, schema, table.name);
}

OperatorPtr Optimizer::plan(const SelectStmt& stmt) {
  TableInfo* from = catalog_.get(stmt.from_table);
  if (!from) throw std::runtime_error("unknown table: " + stmt.from_table);

  OperatorPtr op;
  if (!stmt.has_join) {
    op = build_scan(*from, stmt.where.get());
  } else {
    TableInfo* join = catalog_.get(stmt.join_table);
    if (!join) throw std::runtime_error("unknown table: " + stmt.join_table);

    // Join order: scan the smaller table on the outside to minimize rescans.
    TableInfo* outer = from;
    TableInfo* inner = join;
    if (inner->storage->stats().row_count < outer->storage->stats().row_count)
      std::swap(outer, inner);

    auto outer_scan = std::make_unique<SeqScan>(*outer->storage, qualified_schema(*outer), outer->name);
    auto inner_scan = std::make_unique<SeqScan>(*inner->storage, qualified_schema(*inner), inner->name);
    op = std::make_unique<NestedLoopJoin>(std::move(outer_scan), std::move(inner_scan),
                                          stmt.join_on.get());
  }

  // The full WHERE is always re-applied here (an index range can be a superset).
  if (stmt.where) op = std::make_unique<Filter>(std::move(op), stmt.where.get());
  return std::make_unique<Project>(std::move(op), stmt.select_list);
}

}  // namespace minidb
