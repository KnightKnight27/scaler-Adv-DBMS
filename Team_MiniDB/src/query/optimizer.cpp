#include "optimizer.h"

#include <cmath>
#include <sstream>
#include <stdexcept>

namespace minidb {

namespace {

double EstimateSelectivity(CompareOp op) {
  switch (op) {
    case CompareOp::EQ: return 0.1;
    case CompareOp::NEQ: return 0.9;
    default: return 0.33;  // range predicates
  }
}

// Best-effort alias a comparison operand refers to; empty string means
// "unqualified" (only meaningful for single-table queries).
std::optional<std::string> AliasOf(const Operand &o) {
  if (!o.is_column) return std::nullopt;
  return o.column.table;
}

ResolvedPredicate Resolve(const Comparison &c, const RowSchema &schema) {
  ResolvedPredicate rp;
  int li = schema.Resolve(c.left.column.table, c.left.column.column);
  if (li < 0) throw std::runtime_error("unknown column in predicate: " + c.left.column.column);
  rp.left_idx = li;
  rp.op = c.op;
  if (c.right.is_column) {
    int ri = schema.Resolve(c.right.column.table, c.right.column.column);
    if (ri < 0) throw std::runtime_error("unknown column in predicate: " + c.right.column.column);
    rp.right_is_col = true;
    rp.right_idx = ri;
  } else {
    rp.right_is_col = false;
    rp.right_literal = c.right.literal;
  }
  return rp;
}

}  // namespace

std::unique_ptr<PlanNode> Optimizer::PlanBaseRelation(TableInfo *table, const std::string &alias,
                                                       const std::vector<Comparison> &applicable_preds,
                                                       std::vector<bool> *consumed, std::string *explain) {
  int pk_idx = table->schema.PrimaryKeyIndex();
  std::string pk_name = pk_idx >= 0 ? table->schema.Columns()[pk_idx].name : "";

  // Look for an equality predicate on the primary key we can satisfy with
  // a single B+Tree probe instead of a full scan.
  for (size_t i = 0; i < applicable_preds.size(); i++) {
    const auto &c = applicable_preds[i];
    if (c.op != CompareOp::EQ || pk_idx < 0) continue;
    bool left_is_pk = c.left.is_column && c.left.column.column == pk_name && !c.right.is_column;
    bool right_is_pk = c.right.is_column && c.right.column.column == pk_name && !c.left.is_column;
    if (left_is_pk || right_is_pk) {
      int64_t key = (left_is_pk ? c.right.literal : c.left.literal).AsInt();
      (*consumed)[i] = true;
      if (explain) {
        std::ostringstream os;
        os << "IndexScan on " << table->name << "." << pk_name << " = " << key
           << "  [cost~" << std::log2((double)table->num_rows + 1) + 1 << " vs SeqScan cost~"
           << table->num_rows << "]\n";
        *explain += os.str();
      }
      return std::make_unique<IndexScanNode>(table, alias, key);
    }
  }
  if (explain) {
    std::ostringstream os;
    os << "SeqScan on " << table->name << "  [cost~" << table->num_rows << "]\n";
    *explain += os.str();
  }
  return std::make_unique<SeqScanNode>(table, alias);
}

std::unique_ptr<PlanNode> Optimizer::PlanSelect(const SelectStmt &stmt, std::string *explain) {
  TableInfo *left_table = catalog_->GetTable(stmt.from_table);
  if (!left_table) throw std::runtime_error("no such table: " + stmt.from_table);

  if (!stmt.has_join) {
    std::vector<bool> consumed(stmt.where.size(), false);
    auto plan = PlanBaseRelation(left_table, stmt.from_alias, stmt.where, &consumed, explain);
    std::vector<ResolvedPredicate> resolved;
    for (size_t i = 0; i < stmt.where.size(); i++)
      if (!consumed[i]) resolved.push_back(Resolve(stmt.where[i], plan->OutputSchema()));
    if (!resolved.empty()) plan = std::make_unique<FilterNode>(std::move(plan), std::move(resolved));

    if (!stmt.select_cols.empty()) {
      std::vector<int> idxs;
      for (auto &c : stmt.select_cols) {
        int idx = plan->OutputSchema().Resolve(c.table, c.column);
        if (idx < 0) throw std::runtime_error("unknown column in SELECT: " + c.column);
        idxs.push_back(idx);
      }
      plan = std::make_unique<ProjectNode>(std::move(plan), std::move(idxs));
    }
    return plan;
  }

  // --- Join path ---
  TableInfo *right_table = catalog_->GetTable(stmt.join_table);
  if (!right_table) throw std::runtime_error("no such table: " + stmt.join_table);

  // Split WHERE predicates into per-side (single alias) and cross-table.
  std::vector<Comparison> left_preds, right_preds, cross_preds;
  for (auto &c : stmt.where) {
    auto la = AliasOf(c.left), ra = c.right.is_column ? AliasOf(c.right) : std::optional<std::string>("");
    bool refs_left = (la && (*la == stmt.from_alias || la->empty())) ||
                      (ra && (*ra == stmt.from_alias || ra->empty()));
    bool refs_right = (la && *la == stmt.join_alias) || (ra && *ra == stmt.join_alias);
    if (refs_left && !refs_right) left_preds.push_back(c);
    else if (refs_right && !refs_left) right_preds.push_back(c);
    else cross_preds.push_back(c);
  }

  std::vector<bool> left_consumed(left_preds.size(), false), right_consumed(right_preds.size(), false);
  auto left_plan = PlanBaseRelation(left_table, stmt.from_alias, left_preds, &left_consumed, explain);
  auto right_plan = PlanBaseRelation(right_table, stmt.join_alias, right_preds, &right_consumed, explain);

  std::vector<ResolvedPredicate> left_resolved, right_resolved;
  for (size_t i = 0; i < left_preds.size(); i++)
    if (!left_consumed[i]) left_resolved.push_back(Resolve(left_preds[i], left_plan->OutputSchema()));
  for (size_t i = 0; i < right_preds.size(); i++)
    if (!right_consumed[i]) right_resolved.push_back(Resolve(right_preds[i], right_plan->OutputSchema()));
  if (!left_resolved.empty()) left_plan = std::make_unique<FilterNode>(std::move(left_plan), left_resolved);
  if (!right_resolved.empty()) right_plan = std::make_unique<FilterNode>(std::move(right_plan), right_resolved);

  // Cardinality estimate after filters (index-scanned relations collapse
  // to a single row since the PK is unique).
  bool left_is_index = left_plan->Describe().rfind("IndexScan", 0) == 0 ||
                        left_plan->Describe().rfind("Filter(IndexScan", 0) == 0;
  bool right_is_index = right_plan->Describe().rfind("IndexScan", 0) == 0 ||
                         right_plan->Describe().rfind("Filter(IndexScan", 0) == 0;
  double left_card = left_is_index ? 1.0 : (double)left_table->num_rows;
  double right_card = right_is_index ? 1.0 : (double)right_table->num_rows;
  if (!left_is_index) for (size_t i = 0; i < left_preds.size(); i++) if (!left_consumed[i]) left_card *= EstimateSelectivity(left_preds[i].op);
  if (!right_is_index) for (size_t i = 0; i < right_preds.size(); i++) if (!right_consumed[i]) right_card *= EstimateSelectivity(right_preds[i].op);
  left_card = std::max(1.0, left_card);
  right_card = std::max(1.0, right_card);

  // Does the join condition equate the join column with the *other*
  // side's primary key? If so we can probe that side's index directly.
  int right_pk_idx = right_table->schema.PrimaryKeyIndex();
  int left_pk_idx = left_table->schema.PrimaryKeyIndex();
  std::string right_pk = right_pk_idx >= 0 ? right_table->schema.Columns()[right_pk_idx].name : "";
  std::string left_pk = left_pk_idx >= 0 ? left_table->schema.Columns()[left_pk_idx].name : "";

  bool right_pk_join = stmt.join_cond.op == CompareOp::EQ && right_pk_idx >= 0 &&
      ((stmt.join_cond.left.column.table == stmt.join_alias && stmt.join_cond.left.column.column == right_pk) ||
       (stmt.join_cond.right.column.table == stmt.join_alias && stmt.join_cond.right.column.column == right_pk));
  bool left_pk_join = stmt.join_cond.op == CompareOp::EQ && left_pk_idx >= 0 &&
      ((stmt.join_cond.left.column.table == stmt.from_alias && stmt.join_cond.left.column.column == left_pk) ||
       (stmt.join_cond.right.column.table == stmt.from_alias && stmt.join_cond.right.column.column == left_pk));

  std::unique_ptr<PlanNode> join_plan;
  if (right_pk_join) {
    // Drive on left, probe right's PK index for each left row.
    auto &outer_schema = left_plan->OutputSchema();
    std::string other_col = stmt.join_cond.left.column.table == stmt.join_alias
                                 ? stmt.join_cond.right.column.column
                                 : stmt.join_cond.left.column.column;
    std::string other_alias = stmt.join_cond.left.column.table == stmt.join_alias
                                   ? stmt.join_cond.right.column.table
                                   : stmt.join_cond.left.column.table;
    int outer_idx = outer_schema.Resolve(other_alias, other_col);
    if (explain) *explain += "JoinOrder: " + stmt.from_table + " (outer, est=" + std::to_string((long)left_card) +
                              ") -> IndexNestedLoopJoin probing " + stmt.join_table + " PK\n";
    join_plan = std::make_unique<IndexNestedLoopJoinNode>(std::move(left_plan), right_table, stmt.join_alias, outer_idx);
  } else if (left_pk_join) {
    auto &outer_schema = right_plan->OutputSchema();
    std::string other_col = stmt.join_cond.left.column.table == stmt.from_alias
                                 ? stmt.join_cond.right.column.column
                                 : stmt.join_cond.left.column.column;
    std::string other_alias = stmt.join_cond.left.column.table == stmt.from_alias
                                   ? stmt.join_cond.right.column.table
                                   : stmt.join_cond.left.column.table;
    int outer_idx = outer_schema.Resolve(other_alias, other_col);
    if (explain) *explain += "JoinOrder: " + stmt.join_table + " (outer, est=" + std::to_string((long)right_card) +
                              ") -> IndexNestedLoopJoin probing " + stmt.from_table + " PK\n";
    join_plan = std::make_unique<IndexNestedLoopJoinNode>(std::move(right_plan), left_table, stmt.from_alias, outer_idx);
  } else {
    // Plain nested-loop: put the smaller estimated relation outer so the
    // (rescanned) inner side is the larger one only as many times as the
    // outer has rows - minimizing total tuple comparisons.
    bool left_outer = left_card <= right_card;
    auto &outer_plan = left_outer ? left_plan : right_plan;
    auto &outer_schema = outer_plan->OutputSchema();
    auto &inner_schema = (left_outer ? right_plan : left_plan)->OutputSchema();
    Comparison cond = stmt.join_cond;
    int li = outer_schema.Resolve(cond.left.column.table, cond.left.column.column);
    int ri;
    ResolvedPredicate rp;
    if (li >= 0) {
      rp.left_idx = li;
      ri = inner_schema.Resolve(cond.right.column.table, cond.right.column.column);
      rp.right_idx = ri + (int)outer_schema.Size();
    } else {
      li = outer_schema.Resolve(cond.right.column.table, cond.right.column.column);
      rp.left_idx = li;
      ri = inner_schema.Resolve(cond.left.column.table, cond.left.column.column);
      rp.right_idx = ri + (int)outer_schema.Size();
    }
    rp.op = cond.op;
    rp.right_is_col = true;
    if (explain) *explain += "JoinOrder: " + std::string(left_outer ? stmt.from_table : stmt.join_table) +
                              " (outer, est=" + std::to_string((long)(left_outer ? left_card : right_card)) +
                              ") -> NestedLoopJoin\n";
    join_plan = std::make_unique<NestedLoopJoinNode>(left_outer ? std::move(left_plan) : std::move(right_plan),
                                                      left_outer ? std::move(right_plan) : std::move(left_plan), rp);
  }

  if (!cross_preds.empty()) {
    std::vector<ResolvedPredicate> resolved;
    for (auto &c : cross_preds) resolved.push_back(Resolve(c, join_plan->OutputSchema()));
    join_plan = std::make_unique<FilterNode>(std::move(join_plan), std::move(resolved));
  }

  if (!stmt.select_cols.empty()) {
    std::vector<int> idxs;
    for (auto &c : stmt.select_cols) {
      int idx = join_plan->OutputSchema().Resolve(c.table, c.column);
      if (idx < 0) throw std::runtime_error("unknown column in SELECT: " + c.column);
      idxs.push_back(idx);
    }
    join_plan = std::make_unique<ProjectNode>(std::move(join_plan), std::move(idxs));
  }
  return join_plan;
}

}  // namespace minidb
