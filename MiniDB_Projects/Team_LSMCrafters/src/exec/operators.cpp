#include "exec/operators.h"
#include <sstream>
#include "exec/eval.h"
#include "storage/tuple.h"

namespace minidb {

namespace {
void explain_into(const PhysicalOperator& op, int depth, std::ostringstream& os) {
  os << std::string(static_cast<std::size_t>(depth) * 2, ' ') << "-> " << op.describe() << "\n";
  for (const PhysicalOperator* child : op.children()) explain_into(*child, depth + 1, os);
}
}  // namespace

std::string explain_plan(const PhysicalOperator& root) {
  std::ostringstream os;
  explain_into(root, 0, os);
  return os.str();
}

// ---- SeqScan ----
SeqScan::SeqScan(StorageEngine& store, Schema schema, std::string table)
    : store_(store), schema_(std::move(schema)), table_(std::move(table)) {}

void SeqScan::open() { cursor_ = store_.scan(); }
void SeqScan::close() { cursor_.reset(); }

bool SeqScan::next(Row& out) {
  Key key;
  Bytes value;
  if (!cursor_->next(key, value)) return false;
  out = deserialize(value, schema_);
  return true;
}

// ---- IndexScan ----
IndexScan::IndexScan(StorageEngine& store, Schema schema, std::string table, Key lo, Key hi)
    : store_(store), schema_(std::move(schema)), table_(std::move(table)), lo_(lo), hi_(hi) {}

void IndexScan::open() { cursor_ = store_.index_range(lo_, hi_); }
void IndexScan::close() { cursor_.reset(); }

bool IndexScan::next(Row& out) {
  Key key;
  Bytes value;
  if (!cursor_->next(key, value)) return false;
  out = deserialize(value, schema_);
  return true;
}

std::string IndexScan::describe() const {
  return "IndexScan(" + table_ + ", key in [" + std::to_string(lo_) + ", " +
         std::to_string(hi_) + "])";
}

// ---- Filter ----
Filter::Filter(OperatorPtr child, const Expr* predicate)
    : child_(std::move(child)), predicate_(predicate) {}

bool Filter::next(Row& out) {
  while (child_->next(out)) {
    if (eval_predicate(predicate_, out, child_->out_schema())) return true;
  }
  return false;
}

// ---- NestedLoopJoin ----
NestedLoopJoin::NestedLoopJoin(OperatorPtr outer, OperatorPtr inner, const Expr* on)
    : outer_(std::move(outer)), inner_(std::move(inner)), on_(on) {
  schema_.columns = outer_->out_schema().columns;
  const auto& inner_cols = inner_->out_schema().columns;
  schema_.columns.insert(schema_.columns.end(), inner_cols.begin(), inner_cols.end());
}

void NestedLoopJoin::open() {
  outer_->open();
  have_outer_ = false;
}

bool NestedLoopJoin::next(Row& out) {
  while (true) {
    if (!have_outer_) {
      if (!outer_->next(outer_row_)) return false;
      inner_->open();
      have_outer_ = true;
    }
    Row inner_row;
    if (inner_->next(inner_row)) {
      Row combined = outer_row_;
      combined.insert(combined.end(), inner_row.begin(), inner_row.end());
      if (!on_ || eval_predicate(on_, combined, schema_)) {
        out = std::move(combined);
        return true;
      }
    } else {
      inner_->close();
      have_outer_ = false;  // advance to the next outer row on the next call
    }
  }
}

void NestedLoopJoin::close() {
  if (have_outer_) inner_->close();
  outer_->close();
  have_outer_ = false;
}

// ---- Project ----
Project::Project(OperatorPtr child, const std::vector<ColumnRef>& columns)
    : child_(std::move(child)) {
  const Schema& in = child_->out_schema();
  if (columns.empty()) {  // "*" : keep everything
    for (int i = 0; i < static_cast<int>(in.columns.size()); ++i) indices_.push_back(i);
  } else {
    for (const ColumnRef& ref : columns) indices_.push_back(resolve_column(in, ref));
  }
  for (int i : indices_) schema_.columns.push_back(in.columns[i]);
}

bool Project::next(Row& out) {
  Row row;
  if (!child_->next(row)) return false;
  out.clear();
  for (int i : indices_) out.push_back(row[i]);
  return true;
}

}  // namespace minidb
