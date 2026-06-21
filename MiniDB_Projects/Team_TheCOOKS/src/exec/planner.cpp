#include "exec/planner.h"

#include <cctype>
#include <set>
#include <stdexcept>
#include <utility>

#include "parser/parser.h"  // expr_to_string

namespace walterdb {

namespace {

constexpr uint64_t kRowsPerPageEst = 40;  // rough heap rows per 4 KB page

uint64_t est_pages(uint64_t rows) { return rows == 0 ? 1 : (rows + kRowsPerPageEst - 1) / kRowsPerPageEst; }

// Recursively check that every column reference in `e` resolves uniquely.
void validate_expr(const Expr* e, const ResultSchema& schema) {
  if (!e) return;
  switch (e->kind) {
    case ExprKind::Literal:
      return;
    case ExprKind::ColumnRef: {
      const auto* c = static_cast<const ColumnRefExpr*>(e);
      bool ambiguous = false;
      auto idx = schema.resolve(c->table, c->column, &ambiguous);
      if (!idx) {
        throw std::runtime_error("unknown column '" +
                                 (c->table.empty() ? c->column : c->table + "." + c->column) + "'");
      }
      if (ambiguous) throw std::runtime_error("ambiguous column reference '" + c->column + "'");
      return;
    }
    case ExprKind::Unary:
      validate_expr(static_cast<const UnaryExpr*>(e)->operand.get(), schema);
      return;
    case ExprKind::Binary: {
      const auto* b = static_cast<const BinaryExpr*>(e);
      validate_expr(b->left.get(), schema);
      validate_expr(b->right.get(), schema);
      return;
    }
  }
}

// Best-effort output type for a projected expression (used only for display
// metadata, so approximate is fine).
TypeId infer_type(const Expr* e, const ResultSchema& schema) {
  switch (e->kind) {
    case ExprKind::Literal:
      return static_cast<const LiteralExpr*>(e)->value.type();
    case ExprKind::ColumnRef: {
      const auto* c = static_cast<const ColumnRefExpr*>(e);
      auto idx = schema.resolve(c->table, c->column);
      return idx ? schema.at(*idx).type : TypeId::Integer;
    }
    case ExprKind::Unary: {
      const auto* u = static_cast<const UnaryExpr*>(e);
      return u->op == UnOp::Not ? TypeId::Boolean : infer_type(u->operand.get(), schema);
    }
    case ExprKind::Binary: {
      const auto* b = static_cast<const BinaryExpr*>(e);
      if (b->op >= BinOp::Eq && b->op <= BinOp::Or) return TypeId::Boolean;
      TypeId lt = infer_type(b->left.get(), schema), rt = infer_type(b->right.get(), schema);
      return (lt == TypeId::Integer && rt == TypeId::Integer) ? TypeId::Integer : TypeId::Double;
    }
  }
  return TypeId::Integer;
}

// If `e` (walking AND conjuncts) contains `pk_col = <literal>`, return the
// literal coerced to the PK type.  This is what enables the IndexScan choice.
bool find_pk_equality(const Expr* e, const std::string& pk_name, const std::string& qualifier,
                      TypeId pk_type, Value* out_key) {
  if (!e) return false;
  if (e->kind == ExprKind::Binary) {
    const auto* b = static_cast<const BinaryExpr*>(e);
    if (b->op == BinOp::And) {
      return find_pk_equality(b->left.get(), pk_name, qualifier, pk_type, out_key) ||
             find_pk_equality(b->right.get(), pk_name, qualifier, pk_type, out_key);
    }
    if (b->op == BinOp::Eq) {
      auto is_pk = [&](const Expr* x) {
        if (x->kind != ExprKind::ColumnRef) return false;
        const auto* c = static_cast<const ColumnRefExpr*>(x);
        if (!(c->table.empty() || c->table == qualifier)) return false;
        // case-insensitive column-name match
        if (c->column.size() != pk_name.size()) return false;
        for (size_t i = 0; i < pk_name.size(); ++i)
          if (std::tolower((unsigned char)c->column[i]) != std::tolower((unsigned char)pk_name[i]))
            return false;
        return true;
      };
      const Expr* col = nullptr;
      const Expr* lit = nullptr;
      if (is_pk(b->left.get()) && b->right->kind == ExprKind::Literal) { col = b->left.get(); lit = b->right.get(); }
      else if (is_pk(b->right.get()) && b->left->kind == ExprKind::Literal) { col = b->right.get(); lit = b->left.get(); }
      if (col && lit) {
        auto coerced = coerce(static_cast<const LiteralExpr*>(lit)->value, pk_type);
        if (coerced) { *out_key = *coerced; return true; }
      }
    }
  }
  return false;
}

std::string output_name(const SelectItem& item) {
  if (!item.alias.empty()) return item.alias;
  if (item.expr && item.expr->kind == ExprKind::ColumnRef)
    return static_cast<const ColumnRefExpr*>(item.expr.get())->column;
  return expr_to_string(item.expr.get());
}

std::string to_lower(const std::string& s) {
  std::string o;
  o.reserve(s.size());
  for (char c : s) o.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  return o;
}

// Collect the (lowercased) table qualifiers a predicate references, so the
// greedy join planner knows when a join condition becomes applicable.
void collect_qualifiers(const Expr* e, std::set<std::string>& out) {
  if (!e) return;
  switch (e->kind) {
    case ExprKind::ColumnRef: {
      const auto* c = static_cast<const ColumnRefExpr*>(e);
      if (!c->table.empty()) out.insert(to_lower(c->table));
      return;
    }
    case ExprKind::Unary:
      collect_qualifiers(static_cast<const UnaryExpr*>(e)->operand.get(), out);
      return;
    case ExprKind::Binary: {
      const auto* b = static_cast<const BinaryExpr*>(e);
      collect_qualifiers(b->left.get(), out);
      collect_qualifiers(b->right.get(), out);
      return;
    }
    case ExprKind::Literal:
      return;
  }
}

// One base relation participating in a (multi-table) query.
struct Relation {
  Table* table;
  std::string qualifier;
  uint64_t rows;
};

}  // namespace

Table* Planner::require_table(const std::string& name) {
  Table* t = catalog_.open_table(name);
  if (!t) throw std::runtime_error("no such table '" + name + "'");
  return t;
}

OperatorPtr Planner::make_seq_scan(Table* t, const std::string& qualifier) {
  uint64_t rows = t->info()->row_count;
  std::string desc = "SeqScan(" + t->info()->name +
                     (qualifier != t->info()->name ? " AS " + qualifier : "") +
                     ") [rows~" + std::to_string(rows) + ", cost~" + std::to_string(est_pages(rows)) +
                     " pages]";
  return std::make_unique<SeqScanOp>(t, qualifier, std::move(desc));
}

OperatorPtr Planner::make_index_scan(Table* t, const std::string& qualifier, Value key) {
  int h = t->index() ? t->index()->height() : 1;
  std::string desc = "IndexScan(" + t->info()->name + " USING pk, " +
                     t->schema().column(static_cast<size_t>(t->info()->pk_column)).name + " = " +
                     key.to_string() + ") [rows~1, cost~" + std::to_string(h + 1) + "]";
  return std::make_unique<IndexScanOp>(t, qualifier, std::move(key), std::move(desc));
}

PlannedQuery Planner::plan_select(const SelectStmt& stmt) {
  // --- FROM scan, with the index-vs-seq choice for single-table queries ---
  Table* from_tbl = require_table(stmt.from.table);
  std::string from_q = stmt.from.alias.empty() ? stmt.from.table : stmt.from.alias;

  OperatorPtr root;
  ResultSchema accum;
  // Relations in the ORIGINAL written order, used so that `SELECT *` keeps its
  // intuitive column order even when the join order is reshuffled below.
  std::vector<std::pair<std::string, const Schema*>> star_order;

  if (stmt.joins.empty()) {
    // ---- single table: the index-vs-seq scan choice ----
    if (stmt.where && from_tbl->has_index()) {
      int pk_col = from_tbl->info()->pk_column;
      const std::string& pk_name = from_tbl->schema().column(static_cast<size_t>(pk_col)).name;
      Value key;
      if (find_pk_equality(stmt.where.get(), pk_name, from_q,
                           from_tbl->schema().column(static_cast<size_t>(pk_col)).type, &key)) {
        root = make_index_scan(from_tbl, from_q, std::move(key));
      }
    }
    if (!root) root = make_seq_scan(from_tbl, from_q);
    accum = root->schema();
    star_order.push_back({from_q, &from_tbl->schema()});
  } else {
    // ---- multi-table: greedy join ordering ----
    // Gather all base relations + their ON predicates, then build a left-deep
    // tree starting from the smallest relation, adding at each step the
    // smallest relation that a still-unapplied predicate can connect to the
    // already-joined set.  Each ON predicate is applied as soon as both of its
    // tables are present (as the join's ON, or as a Filter just above it).
    std::vector<Relation> rels;
    rels.push_back({from_tbl, from_q, from_tbl->info()->row_count});
    star_order.push_back({from_q, &from_tbl->schema()});
    std::vector<const Expr*> preds;
    for (const JoinClause& j : stmt.joins) {
      Table* rt = require_table(j.right.table);
      std::string rq = j.right.alias.empty() ? j.right.table : j.right.alias;
      rels.push_back({rt, rq, rt->info()->row_count});
      star_order.push_back({rq, &rt->schema()});
      preds.push_back(j.on.get());
    }
    std::vector<std::set<std::string>> pquals(preds.size());
    for (size_t p = 0; p < preds.size(); ++p) collect_qualifiers(preds[p], pquals[p]);

    std::vector<bool> used(rels.size(), false), applied(preds.size(), false);
    std::set<std::string> joined;

    size_t first = 0;
    for (size_t i = 1; i < rels.size(); ++i)
      if (rels[i].rows < rels[first].rows) first = i;
    used[first] = true;
    joined.insert(to_lower(rels[first].qualifier));
    root = make_seq_scan(rels[first].table, rels[first].qualifier);
    accum = root->schema();

    for (size_t step = 1; step < rels.size(); ++step) {
      // Choose the next relation: prefer one a predicate can connect, smallest first.
      int pick = -1;
      bool pick_conn = false;
      uint64_t pick_rows = 0;
      for (size_t i = 0; i < rels.size(); ++i) {
        if (used[i]) continue;
        std::string ql = to_lower(rels[i].qualifier);
        bool conn = false;
        for (size_t p = 0; p < preds.size() && !conn; ++p) {
          if (applied[p] || !pquals[p].count(ql)) continue;
          bool ok = true;  // applicable iff all other quals already joined
          for (const auto& q : pquals[p])
            if (q != ql && !joined.count(q)) { ok = false; break; }
          conn = ok;
        }
        if (pick == -1 || (conn && !pick_conn) ||
            (conn == pick_conn && rels[i].rows < pick_rows)) {
          pick = static_cast<int>(i);
          pick_conn = conn;
          pick_rows = rels[i].rows;
        }
      }

      OperatorPtr scan = make_seq_scan(rels[pick].table, rels[pick].qualifier);
      joined.insert(to_lower(rels[pick].qualifier));
      used[pick] = true;
      ResultSchema joined_schema = accum.concat(scan->schema());

      // Predicates now fully covered: first becomes the ON, the rest Filters.
      const Expr* on_pred = nullptr;
      std::vector<const Expr*> extra;
      for (size_t p = 0; p < preds.size(); ++p) {
        if (applied[p] || pquals[p].empty()) continue;
        bool ok = true;
        for (const auto& q : pquals[p])
          if (!joined.count(q)) { ok = false; break; }
        if (!ok) continue;
        applied[p] = true;
        if (!on_pred) on_pred = preds[p]; else extra.push_back(preds[p]);
      }
      if (on_pred) validate_expr(on_pred, joined_schema);
      std::string desc = on_pred ? "NestedLoopJoin ON " + expr_to_string(on_pred)
                                 : "NestedLoopJoin (cross product)";
      root = std::make_unique<NestedLoopJoinOp>(std::move(root), std::move(scan), on_pred,
                                                std::move(desc));
      accum = joined_schema;
      for (const Expr* f : extra) {
        validate_expr(f, accum);
        root = std::make_unique<FilterOp>(std::move(root), f, "Filter: " + expr_to_string(f));
      }
    }
    // Any predicate left unapplied (e.g. fully unqualified) becomes a top filter.
    for (size_t p = 0; p < preds.size(); ++p) {
      if (applied[p]) continue;
      validate_expr(preds[p], accum);
      root = std::make_unique<FilterOp>(std::move(root), preds[p], "Filter: " + expr_to_string(preds[p]));
    }
  }

  // --- WHERE filter (kept even when an IndexScan already used a PK equality;
  //     the redundant check is correct and harmless) ---
  if (stmt.where) {
    validate_expr(stmt.where.get(), accum);
    root = std::make_unique<FilterOp>(std::move(root), stmt.where.get(),
                                      "Filter: " + expr_to_string(stmt.where.get()));
  }

  // --- projection (expand * / t.*) ---
  std::vector<ProjectionOp::Item> items;
  ResultSchema out_schema;
  std::vector<std::string> col_names;
  for (const SelectItem& it : stmt.items) {
    if (it.star) {
      // Expand in the original table order (not the possibly-reordered join
      // order), resolving each column back to its position in `accum`.
      for (const auto& [qual, sch] : star_order) {
        if (!it.star_table.empty() && to_lower(qual) != to_lower(it.star_table)) continue;
        for (const Column& col : sch->columns()) {
          auto idx = accum.resolve(qual, col.name);
          if (!idx) continue;  // should not happen
          items.push_back({nullptr, static_cast<int>(*idx)});
          out_schema.add(accum.at(*idx));
          col_names.push_back(col.name);
        }
      }
      continue;
    }
    validate_expr(it.expr.get(), accum);
    items.push_back({it.expr.get(), -1});
    std::string name = output_name(it);
    out_schema.add(ColumnMeta{"", name, infer_type(it.expr.get(), accum)});
    col_names.push_back(name);
  }

  std::string desc = "Project: ";
  for (size_t i = 0; i < col_names.size(); ++i) { if (i) desc += ", "; desc += col_names[i]; }
  root = std::make_unique<ProjectionOp>(std::move(root), std::move(items), out_schema, std::move(desc));

  PlannedQuery pq;
  pq.column_names = std::move(col_names);
  pq.explain = explain_tree(root.get());
  pq.root = std::move(root);
  return pq;
}

}  // namespace walterdb
