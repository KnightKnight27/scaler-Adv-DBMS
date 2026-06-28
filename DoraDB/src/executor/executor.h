#pragma once

#include "common/types.h"
#include "common/config.h"
#include "parser/parser.h"
#include "storage/heap_file.h"
#include "index/bplus_tree.h"

#include <memory>
#include <optional>
#include <vector>
#include <functional>

// ============================================================
// OutputRow — row data + its RID (needed for DELETE/UPDATE)
// ============================================================
struct OutputRow {
    Row values;
    RID rid;
};

// ============================================================
// PlanNode — Volcano-style iterator base class
// ============================================================
class PlanNode {
public:
    virtual ~PlanNode() = default;
    virtual void Open() = 0;
    virtual bool Next(OutputRow& out) = 0;
    virtual void Close() = 0;
};

// ============================================================
// SeqScanNode — full table scan via heap file
// ============================================================
class SeqScanNode : public PlanNode {
public:
    SeqScanNode(HeapFile* heap, const Schema& schema);
    void Open() override;
    bool Next(OutputRow& out) override;
    void Close() override;
private:
    HeapFile* heap_;
    Schema schema_;
    std::vector<OutputRow> rows_;
    int cursor_ = 0;
};

// ============================================================
// IndexScanNode — B+Tree lookup then fetch by RID
// ============================================================
class IndexScanNode : public PlanNode {
public:
    // exact=true: lookup single key. exact=false: range [low,high]
    IndexScanNode(BPlusTree* index, HeapFile* heap, const Schema& schema,
                  int low_key, int high_key, bool exact);
    void Open() override;
    bool Next(OutputRow& out) override;
    void Close() override;
private:
    BPlusTree* index_;
    HeapFile* heap_;
    Schema schema_;
    int low_key_, high_key_;
    bool exact_;
    std::vector<OutputRow> rows_;
    int cursor_ = 0;
};

// ============================================================
// FilterNode — evaluates WHERE predicate, skips non-matching
// ============================================================
class FilterNode : public PlanNode {
public:
    FilterNode(std::unique_ptr<PlanNode> child, ExprPtr predicate, const Schema& schema);
    void Open() override;
    bool Next(OutputRow& out) override;
    void Close() override;
private:
    std::unique_ptr<PlanNode> child_;
    ExprPtr pred_;
    Schema schema_;
};

// ============================================================
// ProjectionNode — selects specific columns
// ============================================================
class ProjectionNode : public PlanNode {
public:
    ProjectionNode(std::unique_ptr<PlanNode> child, std::vector<int> col_indices);
    void Open() override;
    bool Next(OutputRow& out) override;
    void Close() override;
private:
    std::unique_ptr<PlanNode> child_;
    std::vector<int> col_indices_;
};

// ============================================================
// NestedLoopJoinNode — equi-join (for each outer, scan inner)
// ============================================================
class NestedLoopJoinNode : public PlanNode {
public:
    NestedLoopJoinNode(std::unique_ptr<PlanNode> outer, std::unique_ptr<PlanNode> inner,
                       int outer_col, int inner_col);
    void Open() override;
    bool Next(OutputRow& out) override;
    void Close() override;
private:
    std::unique_ptr<PlanNode> outer_, inner_;
    int outer_col_, inner_col_;
    OutputRow current_outer_;
    bool has_outer_ = false;
};

// ============================================================
// Expression evaluation helpers
// ============================================================

// Evaluate an expression to a Value (for column refs and literals)
Value EvalValue(const ExprPtr& expr, const Row& row, const Schema& schema);

// Evaluate a boolean expression (comparison, AND, OR)
bool EvaluateExpr(const ExprPtr& expr, const Row& row, const Schema& schema);

// Overload for JOIN: evaluate with two schemas (left table + right table)
Value EvalValueJoin(const ExprPtr& expr, const Row& row,
                    const Schema& left_schema, const std::string& left_table,
                    const Schema& right_schema, const std::string& right_table);
bool EvaluateExprJoin(const ExprPtr& expr, const Row& row,
                      const Schema& left_schema, const std::string& left_table,
                      const Schema& right_schema, const std::string& right_table);
