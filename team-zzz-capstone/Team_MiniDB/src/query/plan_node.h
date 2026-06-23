#pragma once
#include <memory>
#include <vector>

#include "../catalog/catalog.h"
#include "../sql/ast.h"
#include "row.h"

namespace minidb {

// Volcano-style iterator interface: Open() resets state, Next() pulls one
// row at a time (or returns nullopt at EOF), Close() releases resources.
// This is what the cost-based optimizer produces and what the CLI drives.
class PlanNode {
 public:
  virtual ~PlanNode() = default;
  virtual void Open() = 0;
  virtual std::optional<Row> Next() = 0;
  virtual void Close() {}
  virtual const RowSchema &OutputSchema() const = 0;
  virtual std::string Describe() const = 0;  // for EXPLAIN-style output
};

// Full table scan: walks every live tuple in the heap file.
class SeqScanNode : public PlanNode {
 public:
  SeqScanNode(TableInfo *table, std::string alias) : table_(table), alias_(std::move(alias)) {
    for (auto &col : table_->schema.Columns()) schema_.AddColumn(alias_, col.name);
  }
  void Open() override {
    rows_.clear();
    table_->heap->Scan([&](RID rid, const std::string &data) {
      Tuple t = Tuple::Deserialize(data, table_->schema);
      rows_.push_back(Row{t.Values(), rid});
    });
    idx_ = 0;
  }
  std::optional<Row> Next() override {
    if (idx_ >= rows_.size()) return std::nullopt;
    return rows_[idx_++];
  }
  const RowSchema &OutputSchema() const override { return schema_; }
  std::string Describe() const override { return "SeqScan(" + table_->name + " AS " + alias_ + ")"; }

 private:
  TableInfo *table_;
  std::string alias_;
  RowSchema schema_;
  std::vector<Row> rows_;
  size_t idx_ = 0;
};

// Point lookup through the primary-key B+Tree index - chosen by the
// optimizer when a query has an equality predicate on the indexed column.
class IndexScanNode : public PlanNode {
 public:
  IndexScanNode(TableInfo *table, std::string alias, int64_t key)
      : table_(table), alias_(std::move(alias)), key_(key) {
    for (auto &col : table_->schema.Columns()) schema_.AddColumn(alias_, col.name);
  }
  void Open() override {
    done_ = false;
    auto rid = table_->index->Search(key_);
    if (rid) {
      std::string data;
      if (table_->heap->GetTuple(*rid, &data)) {
        Tuple t = Tuple::Deserialize(data, table_->schema);
        result_ = Row{t.Values(), *rid};
      }
    }
  }
  std::optional<Row> Next() override {
    if (done_ || !result_) {
      done_ = true;
      return std::nullopt;
    }
    done_ = true;
    return result_;
  }
  const RowSchema &OutputSchema() const override { return schema_; }
  std::string Describe() const override {
    return "IndexScan(" + table_->name + " AS " + alias_ + ", key=" + std::to_string(key_) + ")";
  }

 private:
  TableInfo *table_;
  std::string alias_;
  int64_t key_;
  RowSchema schema_;
  std::optional<Row> result_;
  bool done_ = false;
};

// Evaluates a fixed-value comparison against an already-materialized
// resolved column index - built by the planner so the hot path never does
// string lookups per row.
struct ResolvedPredicate {
  int left_idx;       // index into child's output row
  CompareOp op;
  bool right_is_col;
  int right_idx;       // valid if right_is_col
  Value right_literal;  // valid if !right_is_col
};

bool EvalPredicate(const ResolvedPredicate &p, const Row &row);

class FilterNode : public PlanNode {
 public:
  FilterNode(std::unique_ptr<PlanNode> child, std::vector<ResolvedPredicate> preds)
      : child_(std::move(child)), preds_(std::move(preds)) {}
  void Open() override { child_->Open(); }
  std::optional<Row> Next() override {
    while (auto row = child_->Next()) {
      bool ok = true;
      for (auto &p : preds_) {
        if (!EvalPredicate(p, *row)) { ok = false; break; }
      }
      if (ok) return row;
    }
    return std::nullopt;
  }
  void Close() override { child_->Close(); }
  const RowSchema &OutputSchema() const override { return child_->OutputSchema(); }
  std::string Describe() const override { return "Filter(" + child_->Describe() + ")"; }

 private:
  std::unique_ptr<PlanNode> child_;
  std::vector<ResolvedPredicate> preds_;
};

// Naive (block) nested-loop join: for each outer row, rescans the inner
// child from scratch. Used when the join predicate isn't an equality on
// the inner table's primary key (see IndexNestedLoopJoinNode otherwise).
class NestedLoopJoinNode : public PlanNode {
 public:
  NestedLoopJoinNode(std::unique_ptr<PlanNode> outer, std::unique_ptr<PlanNode> inner, ResolvedPredicate cond)
      : outer_(std::move(outer)), inner_(std::move(inner)), cond_(cond) {
    schema_.Append(outer_->OutputSchema());
    schema_.Append(inner_->OutputSchema());
  }
  void Open() override {
    outer_->Open();
    cur_outer_ = outer_->Next();
    inner_->Open();
  }
  std::optional<Row> Next() override {
    while (cur_outer_) {
      while (auto in = inner_->Next()) {
        Row combined;
        combined.values = cur_outer_->values;
        combined.values.insert(combined.values.end(), in->values.begin(), in->values.end());
        ResolvedPredicate joined = cond_;
        if (EvalPredicate(joined, combined)) return combined;
      }
      cur_outer_ = outer_->Next();
      inner_->Close();
      inner_->Open();
    }
    return std::nullopt;
  }
  void Close() override { outer_->Close(); inner_->Close(); }
  const RowSchema &OutputSchema() const override { return schema_; }
  std::string Describe() const override {
    return "NestedLoopJoin(" + outer_->Describe() + ", " + inner_->Describe() + ")";
  }

 private:
  std::unique_ptr<PlanNode> outer_, inner_;
  ResolvedPredicate cond_;
  RowSchema schema_;
  std::optional<Row> cur_outer_;
};

// Index nested-loop join: outer side driven by a scan, inner side probed
// directly via its primary-key B+Tree for each outer row - O(N log M)
// instead of O(N*M). Chosen by the optimizer when the join predicate
// equates outer's join column with inner's primary key.
class IndexNestedLoopJoinNode : public PlanNode {
 public:
  IndexNestedLoopJoinNode(std::unique_ptr<PlanNode> outer, TableInfo *inner_table, std::string inner_alias,
                           int outer_join_col_idx)
      : outer_(std::move(outer)), inner_table_(inner_table), inner_alias_(std::move(inner_alias)),
        outer_join_col_idx_(outer_join_col_idx) {
    schema_.Append(outer_->OutputSchema());
    for (auto &col : inner_table_->schema.Columns()) schema_.AddColumn(inner_alias_, col.name);
  }
  void Open() override { outer_->Open(); }
  std::optional<Row> Next() override {
    while (auto row = outer_->Next()) {
      int64_t probe_key = row->values[outer_join_col_idx_].AsInt();
      auto rid = inner_table_->index->Search(probe_key);
      if (!rid) continue;
      std::string data;
      if (!inner_table_->heap->GetTuple(*rid, &data)) continue;
      Tuple t = Tuple::Deserialize(data, inner_table_->schema);
      Row combined;
      combined.values = row->values;
      combined.values.insert(combined.values.end(), t.Values().begin(), t.Values().end());
      return combined;
    }
    return std::nullopt;
  }
  void Close() override { outer_->Close(); }
  const RowSchema &OutputSchema() const override { return schema_; }
  std::string Describe() const override {
    return "IndexNestedLoopJoin(" + outer_->Describe() + ", probe=" + inner_table_->name + ")";
  }

 private:
  std::unique_ptr<PlanNode> outer_;
  TableInfo *inner_table_;
  std::string inner_alias_;
  int outer_join_col_idx_;
  RowSchema schema_;
};

class ProjectNode : public PlanNode {
 public:
  ProjectNode(std::unique_ptr<PlanNode> child, std::vector<int> col_indices)
      : child_(std::move(child)), col_indices_(std::move(col_indices)) {
    auto &child_schema = child_->OutputSchema();
    for (int idx : col_indices_) schema_.AddColumn("", child_schema.UnqualifiedName(idx));
  }
  void Open() override { child_->Open(); }
  std::optional<Row> Next() override {
    auto row = child_->Next();
    if (!row) return std::nullopt;
    Row out;
    out.rid = row->rid;
    for (int idx : col_indices_) out.values.push_back(row->values[idx]);
    return out;
  }
  void Close() override { child_->Close(); }
  const RowSchema &OutputSchema() const override { return schema_; }
  std::string Describe() const override { return "Project(" + child_->Describe() + ")"; }

 private:
  std::unique_ptr<PlanNode> child_;
  std::vector<int> col_indices_;
  RowSchema schema_;
};

}  // namespace minidb
