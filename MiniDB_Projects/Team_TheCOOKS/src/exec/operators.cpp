#include "exec/operators.h"

#include "catalog/tuple.h"

namespace walterdb {

// ----- SeqScan --------------------------------------------------------------

SeqScanOp::SeqScanOp(Table* table, std::string qualifier, std::string desc)
    : table_(table), desc_(std::move(desc)) {
  schema_ = ResultSchema::from_table(table->schema(), qualifier);
}

void SeqScanOp::open() { cursor_.emplace(table_->heap()->scan()); }

std::optional<Row> SeqScanOp::next() {
  if (!cursor_ || !cursor_->valid()) return std::nullopt;
  Tuple t = Tuple::decode(table_->schema(), cursor_->value());
  cursor_->next();
  return t.values();
}

void SeqScanOp::close() { cursor_.reset(); }

// ----- IndexScan (PK point lookup) -----------------------------------------

IndexScanOp::IndexScanOp(Table* table, std::string qualifier, Value key, std::string desc)
    : table_(table), key_(std::move(key)), desc_(std::move(desc)) {
  schema_ = ResultSchema::from_table(table->schema(), qualifier);
}

void IndexScanOp::open() { done_ = false; }

std::optional<Row> IndexScanOp::next() {
  if (done_) return std::nullopt;
  done_ = true;
  auto rid = table_->lookup_pk(key_);
  if (!rid) return std::nullopt;
  auto tuple = table_->get(*rid);
  if (!tuple) return std::nullopt;
  return tuple->values();
}

void IndexScanOp::close() {}

// ----- Filter ---------------------------------------------------------------

FilterOp::FilterOp(OperatorPtr child, const Expr* predicate, std::string desc)
    : child_(std::move(child)), predicate_(predicate), desc_(std::move(desc)) {}

void FilterOp::open() { child_->open(); }

std::optional<Row> FilterOp::next() {
  while (auto r = child_->next()) {
    if (evaluate_predicate(predicate_, *r, child_->schema())) return r;
  }
  return std::nullopt;
}

void FilterOp::close() { child_->close(); }

// ----- Projection -----------------------------------------------------------

ProjectionOp::ProjectionOp(OperatorPtr child, std::vector<Item> items, ResultSchema schema,
                           std::string desc)
    : child_(std::move(child)), items_(std::move(items)), schema_(std::move(schema)),
      desc_(std::move(desc)) {}

void ProjectionOp::open() { child_->open(); }

std::optional<Row> ProjectionOp::next() {
  auto r = child_->next();
  if (!r) return std::nullopt;
  Row out;
  out.reserve(items_.size());
  for (const Item& it : items_) {
    if (it.expr) {
      out.push_back(evaluate(it.expr, *r, child_->schema()));
    } else {
      out.push_back((*r)[it.passthrough]);
    }
  }
  return out;
}

void ProjectionOp::close() { child_->close(); }

// ----- NestedLoopJoin -------------------------------------------------------

NestedLoopJoinOp::NestedLoopJoinOp(OperatorPtr left, OperatorPtr right, const Expr* on,
                                   std::string desc)
    : left_(std::move(left)), right_(std::move(right)), on_(on), desc_(std::move(desc)) {
  schema_ = left_->schema().concat(right_->schema());
}

void NestedLoopJoinOp::open() {
  left_->open();
  // Materialise the entire right input once (block NLJ): every left row is then
  // joined against this in-memory vector, so the right subtree is scanned once
  // rather than re-opened per left row.
  right_->open();
  right_rows_.clear();
  while (auto r = right_->next()) right_rows_.push_back(std::move(*r));
  right_->close();
  rj_ = 0;
  left_cur_ = left_->next();
}

std::optional<Row> NestedLoopJoinOp::next() {
  while (left_cur_) {
    while (rj_ < right_rows_.size()) {
      Row combined = *left_cur_;
      const Row& rr = right_rows_[rj_];
      combined.insert(combined.end(), rr.begin(), rr.end());
      ++rj_;
      if (!on_ || evaluate_predicate(on_, combined, schema_)) return combined;
    }
    left_cur_ = left_->next();
    rj_ = 0;
  }
  return std::nullopt;
}

void NestedLoopJoinOp::close() {
  left_->close();
  right_rows_.clear();
}

// ----- EXPLAIN tree printer -------------------------------------------------

namespace {
void explain_into(const Operator* op, int depth, std::string& out) {
  out.append(static_cast<size_t>(depth) * 2, ' ');
  out += "-> ";
  out += op->describe();
  out += '\n';
  for (const Operator* child : op->children()) explain_into(child, depth + 1, out);
}
}  // namespace

std::string explain_tree(const Operator* op) {
  std::string out;
  explain_into(op, 0, out);
  return out;
}

}  // namespace walterdb
