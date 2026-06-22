#include "exec/executor.h"
#include "record/tuple.h"

namespace minidb {

int ResolveColumn(const OutSchema& schema, const ColRef& ref) {
  int found = -1;
  for (size_t i = 0; i < schema.size(); i++) {
    if (schema[i].name != ref.column) continue;
    if (!ref.table.empty() && schema[i].table != ref.table) continue;
    if (found != -1) throw DBError("ambiguous column: " + ref.column);
    found = static_cast<int>(i);
  }
  if (found == -1) throw DBError("unknown column: " + ref.column);
  return found;
}

bool EvalPredicate(const Row& row, const BoundPredicate& p) {
  const Value& lhs = row[p.lhs_idx];
  const Value& rhs = p.rhs_is_col ? row[p.rhs_idx] : p.rhs_val;
  int c = lhs.Compare(rhs);
  switch (p.op) {
    case CmpOp::EQ: return c == 0;
    case CmpOp::NE: return c != 0;
    case CmpOp::LT: return c < 0;
    case CmpOp::LE: return c <= 0;
    case CmpOp::GT: return c > 0;
    case CmpOp::GE: return c >= 0;
  }
  return false;
}

BoundPredicate BindPredicate(const OutSchema& schema, const Predicate& p) {
  BoundPredicate b;
  b.lhs_idx = ResolveColumn(schema, p.lhs);
  b.op = p.op;
  b.rhs_is_col = p.rhs_is_col;
  if (p.rhs_is_col) b.rhs_idx = ResolveColumn(schema, p.rhs_col);
  else b.rhs_val = p.rhs_val;
  return b;
}

static OutSchema MakeOutSchema(const Schema& s, const std::string& alias) {
  OutSchema out;
  for (auto& c : s.Columns()) out.push_back({alias, c.name, c.type});
  return out;
}

// ---- SeqScan ----
SeqScanExecutor::SeqScanExecutor(RowStore* store, Schema table_schema, std::string alias)
    : store_(store), table_schema_(std::move(table_schema)) {
  out_schema_ = MakeOutSchema(table_schema_, alias);
}
void SeqScanExecutor::Init() { cursor_ = store_->FullScan(); }
bool SeqScanExecutor::Next(Row* out) {
  std::string bytes;
  if (!cursor_->Next(&bytes)) return false;
  *out = Tuple::Deserialize(bytes.data(), table_schema_).Values();
  return true;
}

// ---- IndexScan ----
IndexScanExecutor::IndexScanExecutor(RowStore* store, Schema table_schema,
                                     std::string alias, int64_t low, int64_t high)
    : store_(store), table_schema_(std::move(table_schema)),
      low_(low), high_(high) {
  out_schema_ = MakeOutSchema(table_schema_, alias);
}
void IndexScanExecutor::Init() { cursor_ = store_->RangeScan(low_, high_); }
bool IndexScanExecutor::Next(Row* out) {
  std::string bytes;
  if (!cursor_->Next(&bytes)) return false;
  *out = Tuple::Deserialize(bytes.data(), table_schema_).Values();
  return true;
}

// ---- Filter ----
FilterExecutor::FilterExecutor(std::unique_ptr<Executor> child,
                               std::vector<BoundPredicate> preds)
    : child_(std::move(child)), preds_(std::move(preds)) {}
void FilterExecutor::Init() { child_->Init(); }
bool FilterExecutor::Next(Row* out) {
  Row row;
  while (child_->Next(&row)) {
    bool ok = true;
    for (auto& p : preds_) if (!EvalPredicate(row, p)) { ok = false; break; }
    if (ok) { *out = std::move(row); return true; }
  }
  return false;
}

// ---- NestedLoopJoin ----
NestedLoopJoinExecutor::NestedLoopJoinExecutor(std::unique_ptr<Executor> left,
                                               std::unique_ptr<Executor> right,
                                               Predicate on)
    : left_(std::move(left)), right_(std::move(right)), on_(std::move(on)) {
  out_schema_ = left_->GetSchema();
  const OutSchema& rs = right_->GetSchema();
  out_schema_.insert(out_schema_.end(), rs.begin(), rs.end());
  bound_ = BindPredicate(out_schema_, on_);
}
void NestedLoopJoinExecutor::Init() {
  left_->Init();
  have_left_ = false;
}
bool NestedLoopJoinExecutor::Next(Row* out) {
  while (true) {
    if (!have_left_) {
      if (!left_->Next(&left_row_)) return false;
      right_->Init();
      have_left_ = true;
    }
    Row right_row;
    while (right_->Next(&right_row)) {
      Row combined = left_row_;
      combined.insert(combined.end(), right_row.begin(), right_row.end());
      if (EvalPredicate(combined, bound_)) { *out = std::move(combined); return true; }
    }
    have_left_ = false;  // exhausted right; advance left
  }
}

// ---- Projection ----
ProjectionExecutor::ProjectionExecutor(std::unique_ptr<Executor> child,
                                       const std::vector<ColRef>& cols)
    : child_(std::move(child)) {
  const OutSchema& cs = child_->GetSchema();
  for (auto& ref : cols) {
    int idx = ResolveColumn(cs, ref);
    indexes_.push_back(idx);
    out_schema_.push_back(cs[idx]);
  }
}
void ProjectionExecutor::Init() { child_->Init(); }
bool ProjectionExecutor::Next(Row* out) {
  Row row;
  if (!child_->Next(&row)) return false;
  Row projected;
  projected.reserve(indexes_.size());
  for (int i : indexes_) projected.push_back(row[i]);
  *out = std::move(projected);
  return true;
}

// ---- Count ----
CountExecutor::CountExecutor(std::unique_ptr<Executor> child)
    : child_(std::move(child)) {
  out_schema_.push_back({"", "count", TypeId::INTEGER});
}
void CountExecutor::Init() { child_->Init(); done_ = false; }
bool CountExecutor::Next(Row* out) {
  if (done_) return false;
  int64_t n = 0;
  Row row;
  while (child_->Next(&row)) n++;
  *out = {Value::Int(n)};
  done_ = true;
  return true;
}

}  // namespace minidb
