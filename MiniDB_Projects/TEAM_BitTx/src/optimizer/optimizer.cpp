#include "optimizer/optimizer.h"

#include "catalog/table_heap.h"
#include "common/tuple.h"
#include "parser/ast.h"

#include <cmath>

namespace minidb {

// Cost-model constants tuned for an in-memory MiniDB on a single thread.
// These are intentionally simple: cost is proportional to rows touched.
static constexpr double kSeqScanCpuPerRow = 1.0;
static constexpr double kIndexScanCpuPerRow = 0.4; // ~2.5x faster than seq scan
static constexpr double kFilterCpuPerRow = 0.5;
static constexpr double kJoinCpuPerRow = 3.0;

Optimizer::Cost Optimizer::Estimate(AbstractExecutor* node) const {
  Cost c;
  if (!node)
    return c;
  // We estimate rows from known executor types. Anything unknown
  // defaults to a moderate cost.
  if (auto* scan = dynamic_cast<SeqScanExecutor*>(node)) {
    (void)scan;
    c.rows = 1000.0; // pessimistic default
    auto it = stats_.find(scan ? "" : "");
    (void)it;
    c.cpu = c.rows * kSeqScanCpuPerRow;
    return c;
  }
  if (dynamic_cast<IndexScanExecutor*>(node)) {
    c.rows = 10.0;
    c.cpu = c.rows * kIndexScanCpuPerRow;
    return c;
  }
  if (auto* f = dynamic_cast<FilterExecutor*>(node)) {
    Cost child = Estimate(f->child_.get());
    c.rows = child.rows * 0.3;
    c.cpu = child.cpu + child.rows * kFilterCpuPerRow;
    return c;
  }
  if (auto* j = dynamic_cast<NestedLoopJoinExecutor*>(node)) {
    Cost l = Estimate(j->left_.get());
    Cost r = Estimate(j->right_.get());
    c.rows = l.rows * r.rows * 0.1;
    c.cpu = l.cpu + r.cpu + (l.rows * r.rows) * kJoinCpuPerRow;
    return c;
  }
  if (auto* s = dynamic_cast<SortExecutor*>(node)) {
    Cost child = Estimate(s->child_.get());
    c.rows = child.rows;
    c.cpu = child.cpu + child.rows * std::log2(child.rows + 1.0);
    return c;
  }
  if (auto* a = dynamic_cast<AggregateExecutor*>(node)) {
    Cost child = Estimate(a->child_.get());
    c.rows = 1.0;
    c.cpu = child.cpu;
    return c;
  }
  if (auto* g = dynamic_cast<GroupByExecutor*>(node)) {
    Cost child = Estimate(g->child_.get());
    c.rows = child.rows * 0.5;
    c.cpu = child.cpu;
    return c;
  }
  if (auto* p = dynamic_cast<ProjectExecutor*>(node)) {
    Cost child = Estimate(p->child_.get());
    c.rows = child.rows;
    c.cpu = child.cpu * 0.5;
    return c;
  }
  if (auto* l = dynamic_cast<LimitExecutor*>(node)) {
    Cost child = Estimate(l->child_.get());
    c.rows = std::min(child.rows, (double)l->limit_);
    c.cpu = child.cpu;
    return c;
  }
  // Default fallback.
  c.rows = 100.0;
  c.cpu = 100.0;
  return c;
}

unique_ptr<AbstractExecutor>
Optimizer::TryIndexScan(SeqScanExecutor* scan, FilterExecutor* filter) const {
  if (!catalog_ || !scan || !filter || !filter->expr_)
    return nullptr;

  auto* op = dynamic_cast<BinaryOp*>(filter->expr_.get());
  if (!op || op->op != "=")
    return nullptr;

  ColumnRef* col = dynamic_cast<ColumnRef*>(op->lhs.get());
  Literal* lit = dynamic_cast<Literal*>(op->rhs.get());
  if (!col || !lit) {
    col = dynamic_cast<ColumnRef*>(op->rhs.get());
    lit = dynamic_cast<Literal*>(op->lhs.get());
  }
  if (!col || !lit)
    return nullptr;

  string tableName = "";
  for (const auto& name : catalog_->ListTables()) {
    if (catalog_->GetTable(name) == scan->GetTable()) {
      tableName = name;
      break;
    }
  }
  if (tableName.empty())
    return nullptr;

  // Check if there is an index on this column
  BPlusTree* idx = catalog_->GetIndex(tableName, col->columnName);
  if (!idx)
    return nullptr;

  size_t colIdx = scan->GetTable()->GetSchema().GetColumnIndex(col->columnName);
  return make_unique<IndexScanExecutor>(nullptr, scan->GetTable(), lit->v, colIdx, catalog_, tableName);
}

unique_ptr<AbstractExecutor> Optimizer::Optimize(unique_ptr<AbstractExecutor> root) {
  if (!root)
    return root;
  AbstractExecutor* raw = root.get();
  AbstractExecutor* child = raw->GetChild();
  if (child) {
    auto rewritten = Optimize(unique_ptr<AbstractExecutor>(child));
    raw->SetChild(move(rewritten));
  }
  // Index scan promotion: if root is Filter and child is SeqScan, try swap.
  if (auto* f = dynamic_cast<FilterExecutor*>(raw)) {
    if (auto* seq = dynamic_cast<SeqScanExecutor*>(f->GetChild())) {
      if (auto repl = TryIndexScan(seq, f))
        f->SetChild(move(repl));
    }
  }
  // Join reorder: swap so smaller-cost side drives outer loop.
  if (auto* j = dynamic_cast<NestedLoopJoinExecutor*>(raw)) {
    AbstractExecutor* l = j->left_.get();
    AbstractExecutor* r = j->right_.get();
    Cost lc = Estimate(l);
    Cost rc = Estimate(r);
    if (rc.rows * rc.cpu < lc.rows * lc.cpu) {
      auto tmp = move(j->left_);
      j->left_ = move(j->right_);
      j->right_ = move(tmp);
      size_t lk = j->leftKey_;
      j->leftKey_ = j->rightKey_;
      j->rightKey_ = lk;
    }
  }
  return root;
}

} // namespace minidb