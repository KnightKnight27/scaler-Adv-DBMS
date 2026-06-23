#include "optimizer.h"

#include <algorithm>
#include <functional>
#include <set>
#include <sstream>

namespace minidb {

double Optimizer::table_rows(TableInfo* t) const {
  double n = static_cast<double>(t->index->size());
  return n < 1 ? 1.0 : n;
}

static void collect_aliases(const ExprPtr& e, std::set<std::string>& out) {
  if (!e) return;
  if (e->kind == ExprKind::Column) {
    auto dot = e->col_name.find('.');
    if (dot != std::string::npos) out.insert(e->col_name.substr(0, dot));
  }
  collect_aliases(e->left, out);
  collect_aliases(e->right, out);
}

static void and_conjuncts(const ExprPtr& e, std::vector<ExprPtr>& out) {
  if (!e) return;
  if (e->kind == ExprKind::Binary && e->op == "AND") {
    and_conjuncts(e->left, out);
    and_conjuncts(e->right, out);
  } else {
    out.push_back(e);
  }
}

double Optimizer::selectivity(const ExprPtr& pred, TableInfo* t,
                              const std::string& alias) const {
  if (!pred) return 1.0;
  std::vector<ExprPtr> conj;
  and_conjuncts(pred, conj);
  double sel = 1.0;
  double n = table_rows(t);
  for (const ExprPtr& c : conj) {
    if (c->kind != ExprKind::Binary) continue;
    const std::string& op = c->op;
    bool is_col_lit = c->left && c->right && c->left->kind == ExprKind::Column &&
                      (c->right->kind == ExprKind::IntLit ||
                       c->right->kind == ExprKind::StrLit ||
                       c->right->kind == ExprKind::BoolLit);
    if (!is_col_lit) continue;
    std::string col = c->left->col_name;
    auto dot = col.find('.');
    std::string bare = dot == std::string::npos ? col : col.substr(dot + 1);
    bool is_pk = (t->schema.column(t->pk_index).name == bare);
    if (op == "=") {
      sel *= is_pk ? (1.0 / n) : 0.1;  // unique key vs assumed 10 distinct values
    } else if (op == "!=") {
      sel *= 0.9;
    } else if (op == "<" || op == "<=" || op == ">" || op == ">=") {
      sel *= 1.0 / 3.0;  // textbook default range selectivity
    }
  }
  return std::max(sel, 1.0 / n);
}

bool Optimizer::pk_equality(const ExprPtr& where, TableInfo* t, const std::string& alias,
                            int64_t* key_out) const {
  if (!where) return false;
  std::vector<ExprPtr> conj;
  and_conjuncts(where, conj);
  const std::string& pk_name = t->schema.column(t->pk_index).name;
  for (const ExprPtr& c : conj) {
    if (c->kind != ExprKind::Binary || c->op != "=") continue;
    if (!c->left || !c->right) continue;
    if (c->left->kind == ExprKind::Column && c->right->kind == ExprKind::IntLit) {
      std::string col = c->left->col_name;
      auto dot = col.find('.');
      std::string a = dot == std::string::npos ? "" : col.substr(0, dot);
      std::string bare = dot == std::string::npos ? col : col.substr(dot + 1);
      if (bare == pk_name && (a.empty() || a == alias || a == t->name)) {
        *key_out = c->right->int_val;
        return true;
      }
    }
  }
  return false;
}

PlanPtr Optimizer::build(const SelectStmt& stmt) {
  TableInfo* from = catalog_->get_table(stmt.from.name);
  if (!from) throw DBException("no such table: " + stmt.from.name);
  std::string from_alias = stmt.from.alias.empty() ? stmt.from.name : stmt.from.alias;

  auto scan = std::make_shared<PlanNode>();
  scan->table = from;
  scan->alias = from_alias;
  int64_t key;
  double rows = table_rows(from);
  if (stmt.joins.empty() && pk_equality(stmt.where, from, from_alias, &key)) {
    scan->type = PlanType::IndexScan;
    scan->index_point = true;
    scan->index_key = key;
    scan->est_rows = 1;
    scan->est_cost = 1;
  } else {
    scan->type = PlanType::SeqScan;
    scan->est_rows = rows * selectivity(stmt.where, from, from_alias);
    scan->est_cost = rows;
  }

  PlanPtr root = scan;

  // greedily add the cheapest eligible table
  std::vector<const JoinClause*> remaining;
  for (const auto& j : stmt.joins) remaining.push_back(&j);
  std::set<std::string> included = {from_alias, from->name};

  while (!remaining.empty()) {
    int best = -1;
    double best_rows = 1e18;
    for (size_t i = 0; i < remaining.size(); i++) {
      const JoinClause* j = remaining[i];
      TableInfo* rt = catalog_->get_table(j->right.name);
      if (!rt) throw DBException("no such table: " + j->right.name);
      std::set<std::string> need;
      collect_aliases(j->on, need);
      // eligible if its ON predicate only references already-joined tables (plus its own)
      std::string ralias = j->right.alias.empty() ? j->right.name : j->right.alias;
      bool eligible = true;
      for (const std::string& a : need)
        if (!included.count(a) && a != ralias && a != rt->name) eligible = false;
      double r = table_rows(rt);
      if (eligible && r < best_rows) { best_rows = r; best = static_cast<int>(i); }
    }
    if (best < 0) best = 0;  // fall back to original order on a cycle

    const JoinClause* j = remaining[best];
    remaining.erase(remaining.begin() + best);
    TableInfo* rt = catalog_->get_table(j->right.name);
    std::string ralias = j->right.alias.empty() ? j->right.name : j->right.alias;
    included.insert(ralias);
    included.insert(rt->name);

    auto rscan = std::make_shared<PlanNode>();
    rscan->type = PlanType::SeqScan;
    rscan->table = rt;
    rscan->alias = ralias;
    rscan->est_rows = table_rows(rt);
    rscan->est_cost = table_rows(rt);

    auto join = std::make_shared<PlanNode>();
    join->type = PlanType::NestedLoopJoin;
    join->join_on = j->on;
    join->children = {root, rscan};
    join->est_rows = root->est_rows * rscan->est_rows * 0.1;  // assume 10% match
    join->est_cost = root->est_cost + root->est_rows * rscan->est_cost;
    root = join;
  }

  if (stmt.where) {
    auto filt = std::make_shared<PlanNode>();
    filt->type = PlanType::Filter;
    filt->predicate = stmt.where;
    filt->children = {root};
    filt->est_rows = root->est_rows;  // selectivity already folded into the scans
    filt->est_cost = root->est_cost + root->est_rows;
    root = filt;
  }

  bool has_agg = !stmt.group_by.empty();
  for (const auto& it : stmt.items)
    if (it.expr && it.expr->kind == ExprKind::Agg) has_agg = true;

  auto top = std::make_shared<PlanNode>();
  top->type = has_agg ? PlanType::Aggregation : PlanType::Projection;
  top->items = stmt.items;
  top->group_by = stmt.group_by;
  top->children = {root};
  top->est_rows = has_agg ? (stmt.group_by.empty() ? 1 : root->est_rows * 0.5) : root->est_rows;
  top->est_cost = root->est_cost + root->est_rows;
  return top;
}

std::string Optimizer::explain(const PlanPtr& plan) {
  std::ostringstream os;
  std::function<void(const PlanPtr&, int)> rec = [&](const PlanPtr& n, int depth) {
    for (int i = 0; i < depth; i++) os << "  ";
    os << "-> ";
    switch (n->type) {
      case PlanType::SeqScan: os << "SeqScan(" << n->table->name << ")"; break;
      case PlanType::IndexScan:
        os << "IndexScan(" << n->table->name << " pk=" << n->index_key << ")";
        break;
      case PlanType::Filter: os << "Filter"; break;
      case PlanType::NestedLoopJoin: os << "NestedLoopJoin"; break;
      case PlanType::Projection: os << "Projection"; break;
      case PlanType::Aggregation:
        os << "Aggregation" << (n->group_by.empty() ? "" : " (grouped)");
        break;
    }
    os << "  [est_rows=" << static_cast<long>(n->est_rows + 0.5)
       << " est_cost=" << static_cast<long>(n->est_cost + 0.5) << "]\n";
    for (const auto& c : n->children) rec(c, depth + 1);
  };
  rec(plan, 0);
  return os.str();
}

}  // namespace minidb
