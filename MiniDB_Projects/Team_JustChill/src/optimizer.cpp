// optimizer.cpp — Track 3 (Query & Concurrency): cost-based planner
#include "optimizer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>

namespace minidb {
namespace {

[[noreturn]] void fail(const std::string& msg) {
  throw std::runtime_error("planner error: " + msg);
}

std::string num(double v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.1f", v);
  return buf;
}

// Resolve a column reference within one table's schema. Returns the column
// index, or -1 if absent / the qualifier names a different table.
int resolveCol(const Schema& schema, const std::string& table_name,
               const ColumnRef& ref) {
  if (!ref.table.empty() && ref.table != table_name) return -1;
  for (int i = 0; i < static_cast<int>(schema.size()); ++i)
    if (schema[i].name == ref.column) return i;
  return -1;
}

bool isRangeOp(CompareOp op) {
  return op == CompareOp::Lt || op == CompareOp::Le || op == CompareOp::Gt ||
         op == CompareOp::Ge;
}

// ---- WHERE-expression rendering (for EXPLAIN) ----

std::string opStr(CompareOp op) {
  switch (op) {
    case CompareOp::Eq: return "=";
    case CompareOp::Ne: return "!=";
    case CompareOp::Lt: return "<";
    case CompareOp::Le: return "<=";
    case CompareOp::Gt: return ">";
    case CompareOp::Ge: return ">=";
  }
  return "?";
}

std::string litStr(const LiteralVal& v) {
  return v.type == ValueType::Int ? std::to_string(v.i) : ("'" + v.s + "'");
}

std::string colRefStr(const ColumnRef& c) {
  return c.table.empty() ? c.column : (c.table + "." + c.column);
}

std::string whereToString(const WhereExpr& e) {
  if (e.kind == WhereExpr::Kind::Leaf)
    return colRefStr(e.leaf.col) + " " + opStr(e.leaf.op) + " " +
           litStr(e.leaf.val);
  const std::string sep = e.kind == WhereExpr::Kind::And ? " AND " : " OR ";
  return "(" + whereToString(*e.left) + sep + whereToString(*e.right) + ")";
}

// ---- WHERE-expression analysis ----

// Translate the AST WHERE tree into an executable PredExpr, resolving column
// names to indices against `schema`. A qualifier is honored when it matches
// `tname`; for a combined join schema (tname == "") it is ignored and the
// match falls back to a by-name lookup. Throws on an unknown column.
std::shared_ptr<PredExpr> resolveExpr(const WhereExpr& e, const Schema& schema,
                                      const std::string& tname) {
  if (e.kind == WhereExpr::Kind::Leaf) {
    int col = resolveCol(schema, tname, e.leaf.col);
    if (col < 0) {
      ColumnRef bare{"", e.leaf.col.column};  // retry ignoring the qualifier
      col = resolveCol(schema, "", bare);
    }
    if (col < 0) fail("unknown column '" + e.leaf.col.column + "' in WHERE");
    return PredExpr::makeLeaf(Predicate{col, e.leaf.op, e.leaf.val.toValue()});
  }
  auto l = resolveExpr(*e.left, schema, tname);
  auto r = resolveExpr(*e.right, schema, tname);
  return e.kind == WhereExpr::Kind::And ? PredExpr::makeAnd(l, r)
                                        : PredExpr::makeOr(l, r);
}

// Find a comparison on `table`'s integer primary key that an IndexScan can
// exploit. We descend through AND nodes (every conjunct must hold, so any one
// may narrow the scan) but NOT through OR nodes (a row matching the other
// branch could have any key, so the index cannot bound the scan).
const Condition* findPkConjunct(const WhereExpr& e, Table* table) {
  if (e.kind == WhereExpr::Kind::Leaf) {
    int col = resolveCol(table->schema(), table->name(), e.leaf.col);
    const bool on_pk = col >= 0 && table->has_pk() &&
                       col == table->pk_index() &&
                       table->schema()[col].type == ValueType::Int;
    const bool op_ok = e.leaf.op == CompareOp::Eq || isRangeOp(e.leaf.op);
    if (on_pk && op_ok && e.leaf.val.type == ValueType::Int) return &e.leaf;
    return nullptr;
  }
  if (e.kind == WhereExpr::Kind::And) {
    if (const Condition* l = findPkConjunct(*e.left, table)) return l;
    return findPkConjunct(*e.right, table);
  }
  return nullptr;  // OR
}

// Structural selectivity of a single comparison (no histograms): equality on
// the unique PK is ~1/N; other equalities ~0.1; ranges ~1/3; != ~0.9.
double leafSelectivity(const Condition& c, Table* table, double N) {
  int col = resolveCol(table->schema(), table->name(), c.col);
  const bool on_unique_pk =
      col >= 0 && table->has_pk() && col == table->pk_index();
  switch (c.op) {
    case CompareOp::Eq: return on_unique_pk ? 1.0 / N : 0.10;
    case CompareOp::Ne: return 0.90;
    default: return 0.33;  // range
  }
}

// Selectivity of a whole WHERE tree: AND multiplies, OR uses inclusion-
// exclusion. Assumes independence — coarse, but enough to drive plan choices.
double exprSelectivity(const WhereExpr& e, Table* table, double N) {
  if (e.kind == WhereExpr::Kind::Leaf) return leafSelectivity(e.leaf, table, N);
  const double l = exprSelectivity(*e.left, table, N);
  const double r = exprSelectivity(*e.right, table, N);
  if (e.kind == WhereExpr::Kind::And) return l * r;
  return std::min(1.0, l + r - l * r);  // OR
}

// Record which of the two joined tables each WHERE leaf references. Used to
// decide whether the predicate can be pushed below the join.
void collectTables(const WhereExpr& e, Table* from, Table* joined,
                   bool& uses_from, bool& uses_joined) {
  if (e.kind == WhereExpr::Kind::Leaf) {
    if (resolveCol(from->schema(), from->name(), e.leaf.col) >= 0)
      uses_from = true;
    else if (resolveCol(joined->schema(), joined->name(), e.leaf.col) >= 0)
      uses_joined = true;
    else
      fail("unknown column '" + e.leaf.col.column + "' in WHERE");
    return;
  }
  collectTables(*e.left, from, joined, uses_from, uses_joined);
  collectTables(*e.right, from, joined, uses_from, uses_joined);
}

// A planned sub-tree plus its cost estimate and an EXPLAIN rendering whose
// lines are indented relative to this fragment's own root.
struct Fragment {
  OperatorPtr op;
  double cost = 0.0;
  double est_rows = 0.0;
  std::vector<std::string> explain;  // top-down, fragment-relative
};

std::vector<std::string> indent(const std::vector<std::string>& lines) {
  std::vector<std::string> out;
  out.reserve(lines.size());
  for (const auto& l : lines) out.push_back("  " + l);
  return out;
}

// Wrap `f`'s operator in a Filter for `expr`, prefixing the EXPLAIN with a
// "Filter[…]" line over the (now indented) child fragment.
void addFilter(Fragment& f, const WhereExpr& expr,
               const std::shared_ptr<PredExpr>& pred) {
  f.op = std::make_unique<Filter>(std::move(f.op), pred);
  f.explain = indent(f.explain);
  f.explain.insert(f.explain.begin(), "Filter[" + whereToString(expr) + "]");
}

// Build the cheapest single-table access path for `table` under an optional
// WHERE expression, costing a sequential TableScan against a PK IndexScan and
// re-checking the full predicate with a residual Filter where needed.
Fragment buildAccessPath(Table* table, const ExecContext& ctx,
                         const WhereExpr* expr) {
  const double N = static_cast<double>(table->size());
  const double rows = N < 1.0 ? 1.0 : N;  // avoid log/÷ by zero on empty tables
  const std::string& tname = table->name();

  // Resolve the executable predicate (also validates every column name).
  std::shared_ptr<PredExpr> pred =
      expr ? resolveExpr(*expr, table->schema(), tname) : nullptr;

  const double sel = expr ? exprSelectivity(*expr, table, rows) : 1.0;
  const double scan_cost = rows;                  // touch every row
  const double scan_rows = rows * sel;            // rows surviving the predicate

  // An index-eligible PK conjunct (if any) lets us cost an IndexScan.
  const Condition* pk = expr ? findPkConjunct(*expr, table) : nullptr;
  double index_match = rows, index_cost = 1e18;
  if (pk) {
    index_match = pk->op == CompareOp::Eq ? 1.0 : rows * 0.33;
    index_cost = std::log2(rows) + 1.0 + index_match;
  }
  const bool use_index = pk && index_cost < scan_cost;

  Fragment f;
  if (use_index) {
    // Bounds from the PK conjunct. Eq and the closed-side ranges are exact;
    // Gt/Lt include the bound key, so they need the residual Filter anyway.
    const Key k = pk->val.i;
    Key low = BPlusTree::kMin, high = BPlusTree::kMax;
    switch (pk->op) {
      case CompareOp::Eq: low = high = k; break;
      case CompareOp::Ge:
      case CompareOp::Gt: low = k; break;
      case CompareOp::Le:
      case CompareOp::Lt: high = k; break;
      default: break;  // unreachable
    }
    f.op = std::make_unique<IndexScan>(table, low, high, ctx);
    f.explain.push_back(
        "IndexScan(" + tname + ") PK [" +
        (low == BPlusTree::kMin ? "-inf" : std::to_string(low)) + ", " +
        (high == BPlusTree::kMax ? "+inf" : std::to_string(high)) +
        "]  est_rows=" + num(index_match) + " cost=" + num(index_cost) +
        "  (chosen over TableScan cost=" + num(scan_cost) + ")");

    // The index bounds are exact only when the whole WHERE is this single PK
    // leaf with a closed operator; otherwise re-check the full predicate.
    const bool exact = expr->kind == WhereExpr::Kind::Leaf &&
                       (pk->op == CompareOp::Eq || pk->op == CompareOp::Ge ||
                        pk->op == CompareOp::Le);
    if (!exact) addFilter(f, *expr, pred);

    f.cost = index_cost;
    f.est_rows = scan_rows;
    return f;
  }

  // Sequential scan, with a Filter for the full predicate when present.
  f.op = std::make_unique<TableScan>(table, ctx);
  std::string desc =
      "TableScan(" + tname + ")  est_rows=" + num(scan_rows) + " cost=" + num(scan_cost);
  if (pk) desc += "  (chosen over IndexScan cost=" + num(index_cost) + ")";
  f.explain.push_back(std::move(desc));
  if (expr) addFilter(f, *expr, pred);

  f.cost = scan_cost;
  f.est_rows = scan_rows;
  return f;
}

// Which of the two join columns belongs to `table` (returns its index in that
// table's schema), or -1 if neither does.
int joinColFor(Table* table, const JoinClause& jc) {
  int li = resolveCol(table->schema(), table->name(), jc.left);
  if (li >= 0) return li;
  return resolveCol(table->schema(), table->name(), jc.right);
}

}  // namespace

PhysicalPlan Optimizer::optimize(const Statement& stmt, const ExecContext& ctx) {
  PhysicalPlan plan;

  // ---------------- INSERT ----------------
  if (stmt.isInsert()) {
    const auto& ins = std::get<InsertStatement>(stmt.node);
    Table* t = catalog_.getTable(ins.table);
    if (!t) fail("unknown table '" + ins.table + "'");
    const Schema& schema = t->schema();
    if (ins.values.size() != schema.size())
      fail("arity mismatch for '" + ins.table + "': expected " +
           std::to_string(schema.size()) + " values, got " +
           std::to_string(ins.values.size()));
    Tuple row;
    row.reserve(schema.size());
    for (size_t c = 0; c < schema.size(); ++c) {
      if (ins.values[c].type != schema[c].type)
        fail("type mismatch for column '" + schema[c].name + "'");
      row.push_back(ins.values[c].toValue());
    }
    plan.root = std::make_unique<Insert>(t, std::move(row), ctx);
    plan.is_dml = true;
    plan.cost = 1.0;
    plan.explain = "Insert(" + ins.table + ")";
    return plan;
  }

  // ---------------- DELETE ----------------
  if (stmt.isDelete()) {
    const auto& del = std::get<DeleteStatement>(stmt.node);
    Table* t = catalog_.getTable(del.table);
    if (!t) fail("unknown table '" + del.table + "'");
    Fragment src = buildAccessPath(t, ctx, del.where.get());

    plan.cost = src.cost;
    plan.root = std::make_unique<Delete>(t, std::move(src.op), ctx);
    plan.is_dml = true;

    std::string ex = "Delete(" + del.table + ")  cost=" + num(src.cost) + "\n";
    for (const auto& l : indent(src.explain)) ex += l + "\n";
    plan.explain = ex;
    return plan;
  }

  // ---------------- SELECT ----------------
  const auto& sel = std::get<SelectStatement>(stmt.node);
  Table* from = catalog_.getTable(sel.from);
  if (!from) fail("unknown table '" + sel.from + "'");

  Fragment body;        // access path (single table) or join
  Schema out_schema;    // schema of `body`'s output (for projection resolution)

  if (!sel.join) {
    // Single-table SELECT.
    body = buildAccessPath(from, ctx, sel.where.get());
    out_schema = from->schema();
  } else {
    // Two-table equi-join.
    const JoinClause& jc = *sel.join;
    Table* joined = catalog_.getTable(jc.table);
    if (!joined) fail("unknown table '" + jc.table + "'");

    // Decide where the WHERE applies. A predicate confined to one table is
    // pushed below the join (so it can use that table's index); one spanning
    // both tables is applied as a Filter on the join output.
    const WhereExpr* from_where = nullptr;
    const WhereExpr* joined_where = nullptr;
    const WhereExpr* post_where = nullptr;
    if (sel.where) {
      bool uf = false, uj = false;
      collectTables(*sel.where, from, joined, uf, uj);
      if (uf && !uj) from_where = sel.where.get();
      else if (uj && !uf) joined_where = sel.where.get();
      else post_where = sel.where.get();
    }

    Fragment f_from = buildAccessPath(from, ctx, from_where);
    Fragment f_joined = buildAccessPath(joined, ctx, joined_where);

    // Join order: smaller estimated relation drives the nested loop. Decide
    // the order *before* moving the fragments (a moved-from Fragment has a
    // null op), then move each into its chosen slot exactly once.
    const bool swapped = f_joined.est_rows < f_from.est_rows;
    Table* outer_tbl = swapped ? joined : from;
    Table* inner_tbl = swapped ? from : joined;
    Fragment outer = std::move(swapped ? f_joined : f_from);
    Fragment inner = std::move(swapped ? f_from : f_joined);

    int outer_col = joinColFor(outer_tbl, jc);
    int inner_col = joinColFor(inner_tbl, jc);
    if (outer_col < 0 || inner_col < 0)
      fail("JOIN ON columns must reference the two joined tables");

    const double join_cost =
        outer.cost + outer.est_rows * inner.cost;  // inner re-scanned per outer row
    const double join_rows = std::max(outer.est_rows, inner.est_rows);

    // Combined output schema is outer ++ inner (matches NestedLoopJoin).
    out_schema = outer_tbl->schema();
    const Schema& is = inner_tbl->schema();
    out_schema.insert(out_schema.end(), is.begin(), is.end());

    Fragment jf;
    jf.op = std::make_unique<NestedLoopJoin>(std::move(outer.op),
                                             std::move(inner.op), outer_col,
                                             inner_col);
    jf.cost = join_cost;
    jf.est_rows = join_rows;
    jf.explain.push_back(
        "NestedLoopJoin  outer=" + outer_tbl->name() + " (est_rows=" +
        num(outer.est_rows) + ")  inner=" + inner_tbl->name() + " (est_rows=" +
        num(inner.est_rows) + ")  cost=" + num(join_cost) +
        (swapped ? "  [reordered: smaller relation drives]" : ""));
    for (const auto& l : indent(outer.explain)) jf.explain.push_back(l);
    for (const auto& l : indent(inner.explain)) jf.explain.push_back(l);

    // A cross-table WHERE becomes a Filter over the join output.
    if (post_where)
      addFilter(jf, *post_where, resolveExpr(*post_where, out_schema, ""));

    body = std::move(jf);
  }

  // Projection (SELECT list) on top, unless SELECT *.
  std::vector<std::string> lines;
  if (sel.star) {
    plan.root = std::move(body.op);
    lines = std::move(body.explain);
  } else {
    std::vector<int> cols;
    std::vector<std::string> names;
    for (const auto& ref : sel.columns) {
      // Resolve against the (possibly joined) output schema by column name.
      // Any table qualifier is advisory here: the combined schema's column
      // names are unique in our single-PK demos, so we match on name.
      ColumnRef bare{"", ref.column};
      int idx = resolveCol(out_schema, /*table_name=*/"", bare);
      if (idx < 0) fail("unknown column '" + ref.column + "' in SELECT list");
      cols.push_back(idx);
      names.push_back(out_schema[idx].name);
    }
    plan.root = std::make_unique<Projection>(std::move(body.op), cols);
    std::string head = "Projection [";
    for (size_t k = 0; k < names.size(); ++k) head += (k ? ", " : "") + names[k];
    head += "]";
    lines.push_back(std::move(head));
    for (const auto& l : indent(body.explain)) lines.push_back(l);
  }

  plan.cost = body.cost;
  std::string ex;
  for (const auto& l : lines) ex += l + "\n";
  plan.explain = ex;
  return plan;
}

}  // namespace minidb
