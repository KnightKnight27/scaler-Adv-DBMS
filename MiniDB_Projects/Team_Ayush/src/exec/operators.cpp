#include "exec/operators.h"

#include "index/bplus_tree.h"
#include "record/tuple.h"
#include "storage/heap_file.h"

namespace minidb {

namespace {
// Evaluate `lhs op rhs` using Value::Compare.
bool EvalCompare(const Value& lhs, CompOp op, const Value& rhs) {
  int c = lhs.Compare(rhs);
  switch (op) {
    case CompOp::EQ: return c == 0;
    case CompOp::NE: return c != 0;
    case CompOp::LT: return c < 0;
    case CompOp::LE: return c <= 0;
    case CompOp::GT: return c > 0;
    case CompOp::GE: return c >= 0;
  }
  return false;
}

std::vector<std::string> QualifiedCols(const TableInfo* t) {
  std::vector<std::string> cols;
  for (const Column& c : t->schema.columns) cols.push_back(t->name + "." + c.name);
  return cols;
}
}  // namespace

// ---- SeqScan ---------------------------------------------------------------
SeqScan::SeqScan(BufferPool* bp, const TableInfo* t)
    : bp_(bp), t_(t), idx_(0), rows_read_(0) {
  out_cols_ = QualifiedCols(t);
}

void SeqScan::Open() {
  rows_.clear();
  idx_ = 0;
  rows_read_ = 0;
  HeapFile heap(bp_, t_->heap_first, t_->schema.RecordSize());
  const Schema& schema = t_->schema;
  heap.Scan([&](RID, const char* data) {
    rows_.push_back(tuple::Deserialize(schema, data));
    ++rows_read_;
  });
}

bool SeqScan::Next(Row& out) {
  if (idx_ >= rows_.size()) return false;
  out = rows_[idx_++];
  return true;
}

// ---- IndexScan -------------------------------------------------------------
IndexScan::IndexScan(BufferPool* bp, const TableInfo* t, int32_t low, int32_t high)
    : bp_(bp), t_(t), low_(low), high_(high), idx_(0), rows_read_(0) {
  out_cols_ = QualifiedCols(t);
}

void IndexScan::Open() {
  rows_.clear();
  idx_ = 0;
  rows_read_ = 0;
  BPlusTree tree(bp_, t_->pk_index_header);
  HeapFile heap(bp_, t_->heap_first, t_->schema.RecordSize());
  const Schema& schema = t_->schema;
  std::vector<RID> rids;
  tree.Range(low_, high_, [&](int32_t, RID r) { rids.push_back(r); });
  std::vector<char> buf(schema.RecordSize());
  for (RID r : rids) {
    if (heap.Get(r, buf.data())) {
      rows_.push_back(tuple::Deserialize(schema, buf.data()));
      ++rows_read_;
    }
  }
}

bool IndexScan::Next(Row& out) {
  if (idx_ >= rows_.size()) return false;
  out = rows_[idx_++];
  return true;
}

// ---- Filter ----------------------------------------------------------------
Filter::Filter(std::unique_ptr<Operator> child, std::vector<CompiledPred> preds)
    : child_(std::move(child)), preds_(std::move(preds)) {
  out_cols_ = child_->OutCols();
}

void Filter::Open() { child_->Open(); }

bool Filter::Next(Row& out) {
  Row r;
  while (child_->Next(r)) {
    bool ok = true;
    for (const CompiledPred& p : preds_) {
      if (!EvalCompare(r[p.col_index], p.op, p.value)) { ok = false; break; }
    }
    if (ok) { out = r; return true; }
  }
  return false;
}

// ---- Project ---------------------------------------------------------------
Project::Project(std::unique_ptr<Operator> child, std::vector<int> indices,
                 std::vector<std::string> out_names)
    : child_(std::move(child)), indices_(std::move(indices)) {
  out_cols_ = std::move(out_names);
}

void Project::Open() { child_->Open(); }

bool Project::Next(Row& out) {
  Row r;
  if (!child_->Next(r)) return false;
  out.clear();
  out.reserve(indices_.size());
  for (int i : indices_) out.push_back(r[i]);
  return true;
}

// ---- NestedLoopJoin --------------------------------------------------------
NestedLoopJoin::NestedLoopJoin(
    std::unique_ptr<Operator> outer, int outer_key_idx,
    std::function<std::unique_ptr<Operator>(const Value&)> inner_factory,
    std::vector<std::string> inner_cols)
    : outer_(std::move(outer)),
      outer_key_idx_(outer_key_idx),
      inner_factory_(std::move(inner_factory)),
      inner_cols_(std::move(inner_cols)),
      have_outer_(false) {
  out_cols_ = outer_->OutCols();
  for (const std::string& c : inner_cols_) out_cols_.push_back(c);
}

void NestedLoopJoin::Open() {
  outer_->Open();
  inner_.reset();
  have_outer_ = false;
}

bool NestedLoopJoin::Next(Row& out) {
  while (true) {
    if (!inner_) {
      if (!outer_->Next(cur_outer_)) return false;
      have_outer_ = true;
      inner_ = inner_factory_(cur_outer_[outer_key_idx_]);
      inner_->Open();
    }
    Row ir;
    if (inner_->Next(ir)) {
      out = cur_outer_;
      for (const Value& v : ir) out.push_back(v);
      return true;
    }
    inner_.reset();  // exhausted this inner; advance outer next loop
  }
}

}  // namespace minidb
