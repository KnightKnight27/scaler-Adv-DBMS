#include "execution/executor.h"

namespace minidb {

// ---------------------------- evaluation helpers ---------------------------
int resolveColumn(const vector<ColumnMeta> &cols, const string &table,
                  const string &name) {
  for (size_t i = 0; i < cols.size(); ++i) {
    if (cols[i].name != name) continue;
    if (table.empty() || cols[i].table == table) return static_cast<int>(i);
  }
  return -1;
}

bool evalPredicate(const Predicate &p, const vector<ColumnMeta> &cols,
                   const Tuple &row) {
  int idx = resolveColumn(cols, p.table, p.column);
  if (idx < 0) throw BinderError("unknown column in WHERE: " + p.column);
  int cmp = row.value(idx).compare(p.value);   // <0, 0, >0
  switch (p.op) {
    case CompOp::kEq: return cmp == 0;
    case CompOp::kNe: return cmp != 0;
    case CompOp::kLt: return cmp < 0;
    case CompOp::kLe: return cmp <= 0;
    case CompOp::kGt: return cmp > 0;
    case CompOp::kGe: return cmp >= 0;
  }
  return false;
}

bool evalAll(const vector<Predicate> &ps, const vector<ColumnMeta> &cols,
             const Tuple &row) {
  for (const auto &p : ps)
    if (!evalPredicate(p, cols, row)) return false;
  return true;
}

// Build ColumnMeta list for a whole table (each column qualified by table name).
vector<ColumnMeta> makeTableColumns(TableInfo *t) {
  vector<ColumnMeta> v;
  for (auto &c : t->schema.columns()) v.push_back({t->name, c.name, c.type});
  return v;
}
static vector<ColumnMeta> colsOf(TableInfo *t) { return makeTableColumns(t); }

// --------------------------------- SeqScan ---------------------------------
SeqScanExecutor::SeqScanExecutor(TableInfo *table, vector<Predicate> preds)
    : table_(table), preds_(move(preds)), cols_(colsOf(table)) {}

void SeqScanExecutor::init() {
  it_ = make_unique<TableHeap::Iterator>(table_->heap->begin());
}

bool SeqScanExecutor::next(Tuple *out) {
  while (it_->rid().page_id != INVALID_PAGE_ID) {
    Tuple t = Tuple::deserialize(it_->bytes().data(), table_->schema);
    it_->advance();
    if (evalAll(preds_, cols_, t)) { *out = move(t); return true; }
  }
  return false;
}

// -------------------------------- IndexScan --------------------------------
IndexScanExecutor::IndexScanExecutor(TableInfo *table, IndexInfo *index,
                                     unique_ptr<Value> low,
                                     unique_ptr<Value> high,
                                     vector<Predicate> residual)
    : table_(table), index_(index), low_(move(low)), high_(move(high)),
      residual_(move(residual)), cols_(colsOf(table)) {}

void IndexScanExecutor::init() {
  rids_ = index_->tree->range(low_.get(), high_.get());  // B+ Tree range lookup
  pos_ = 0;
}

bool IndexScanExecutor::next(Tuple *out) {
  while (pos_ < rids_.size()) {
    RID rid = rids_[pos_++];
    string bytes;
    if (!table_->heap->getTuple(rid, &bytes)) continue;   // skip deleted
    Tuple t = Tuple::deserialize(bytes.data(), table_->schema);
    if (evalAll(residual_, cols_, t)) { *out = move(t); return true; }
  }
  return false;
}

// ----------------------------- NestedLoopJoin ------------------------------
NestedLoopJoinExecutor::NestedLoopJoinExecutor(
    unique_ptr<Executor> outer, TableInfo *inner, IndexInfo *inner_index,
    string outer_table, string outer_col, string inner_col,
    vector<Predicate> inner_preds)
    : outer_(move(outer)), inner_(inner), inner_index_(inner_index),
      outer_table_(move(outer_table)), outer_col_(move(outer_col)),
      inner_col_(move(inner_col)), inner_preds_(move(inner_preds)),
      inner_cols_(colsOf(inner)) {
  cols_ = outer_->columns();
  for (auto &c : inner_cols_) cols_.push_back(c);
  outer_join_idx_ = resolveColumn(outer_->columns(), outer_table_, outer_col_);
}

void NestedLoopJoinExecutor::init() {
  outer_->init();
  inner_rows_.clear();
  inner_pos_ = 0;
}

void NestedLoopJoinExecutor::loadInnerMatches(const Tuple &outer_row) {
  inner_rows_.clear();
  Value key = outer_row.value(outer_join_idx_);
  int inner_col_idx = inner_->schema.getColIdx(inner_col_);

  if (inner_index_ != nullptr) {           // INDEX nested-loop join: probe B+ Tree
    for (RID rid : inner_index_->tree->range(&key, &key)) {
      string bytes;
      if (!inner_->heap->getTuple(rid, &bytes)) continue;
      Tuple t = Tuple::deserialize(bytes.data(), inner_->schema);
      if (evalAll(inner_preds_, inner_cols_, t)) inner_rows_.push_back(move(t));
    }
  } else {                                 // plain nested-loop: scan inner heap
    for (auto it = inner_->heap->begin(); it != inner_->heap->end(); it.advance()) {
      Tuple t = Tuple::deserialize(it.bytes().data(), inner_->schema);
      if (t.value(inner_col_idx).compare(key) == 0 &&
          evalAll(inner_preds_, inner_cols_, t))
        inner_rows_.push_back(move(t));
    }
  }
}

bool NestedLoopJoinExecutor::next(Tuple *out) {
  while (true) {
    if (inner_pos_ < inner_rows_.size()) {           // emit next match for current outer
      vector<Value> vals = cur_outer_.values();
      const Tuple &inner = inner_rows_[inner_pos_++];
      for (auto &v : inner.values()) vals.push_back(v);
      *out = Tuple(move(vals));
      return true;
    }
    if (!outer_->next(&cur_outer_)) return false;    // advance to next outer row
    loadInnerMatches(cur_outer_);
    inner_pos_ = 0;
  }
}

// ------------------------------- Projection --------------------------------
ProjectionExecutor::ProjectionExecutor(unique_ptr<Executor> child,
                                       vector<int> col_indexes,
                                       vector<ColumnMeta> out_cols)
    : child_(move(child)), idxs_(move(col_indexes)),
      cols_(move(out_cols)) {}

void ProjectionExecutor::init() { child_->init(); }

bool ProjectionExecutor::next(Tuple *out) {
  Tuple in;
  if (!child_->next(&in)) return false;
  vector<Value> vals;
  for (int i : idxs_) vals.push_back(in.value(i));
  *out = Tuple(move(vals));
  return true;
}

}  // namespace minidb
