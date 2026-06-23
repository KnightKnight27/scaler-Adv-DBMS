// execution.cpp — Track 3 (Query & Concurrency)
#include "execution.h"

#include <stdexcept>

namespace minidb {

// ---- Value ----

bool Value::operator==(const Value& o) const {
  if (type != o.type) return false;
  return type == ValueType::Int ? i == o.i : s == o.s;
}

bool Value::operator<(const Value& o) const {
  // Comparing mixed types is a query error; here we keep it well-defined.
  if (type != o.type) return type < o.type;
  return type == ValueType::Int ? i < o.i : s < o.s;
}

std::string Value::toString() const {
  return type == ValueType::Int ? std::to_string(i) : s;
}

// ---- Predicate ----

bool Predicate::eval(const Tuple& t) const {
  const Value& v = t[column];
  switch (op) {
    case CompareOp::Eq: return v == constant;
    case CompareOp::Ne: return !(v == constant);
    case CompareOp::Lt: return v < constant;
    case CompareOp::Le: return v < constant || v == constant;
    case CompareOp::Gt: return constant < v;
    case CompareOp::Ge: return constant < v || v == constant;
  }
  return false;
}

// ---- Table / Catalog ----

bool tableHasIntPk(const Table& t) {
  return t.has_pk() && t.schema()[t.pk_index()].type == ValueType::Int;
}

RID Table::insert(const Tuple& t) {
  RID rid;
  rid.page_id = static_cast<uint32_t>(records_.size());
  rid.slot_id = 0;
  records_.push_back(Record{t, false});
  if (tableHasIntPk(*this)) {
    pk_tree_.insert(t[pk_index_].i, rid);
  }
  return rid;
}

void Table::markDeleted(RID rid) {
  Record& rec = records_[rid.page_id];
  if (rec.is_deleted) return;
  rec.is_deleted = true;
  if (tableHasIntPk(*this)) {
    pk_tree_.remove(rec.tuple[pk_index_].i);
  }
}

Table* Catalog::createTable(const std::string& name, Schema schema,
                            int pk_index) {
  auto tbl = std::make_unique<Table>(name, std::move(schema), pk_index);
  Table* raw = tbl.get();
  tables_[name] = std::move(tbl);
  return raw;
}

Table* Catalog::getTable(const std::string& name) {
  auto it = tables_.find(name);
  return it == tables_.end() ? nullptr : it->second.get();
}

// ---- TableScan ----

void TableScan::open() {
  ctx_.lock(table_->name(), LockMode::Shared);
  cursor_ = 0;
}

bool TableScan::next(Tuple& out) {
  while (cursor_ < table_->size()) {
    RID rid{static_cast<uint32_t>(cursor_), 0};
    ++cursor_;
    const Record& rec = table_->record(rid);
    if (rec.is_deleted) continue;  // skip tombstoned rows
    out = rec.tuple;
    return true;
  }
  return false;
}

// ---- IndexScan ----

void IndexScan::open() {
  ctx_.lock(table_->name(), LockMode::Shared);
  it_ = table_->index().range(low_, high_);
}

bool IndexScan::next(Tuple& out) {
  while (it_.valid()) {
    RID rid = it_.rid();
    it_.next();
    const Record& rec = table_->record(rid);
    if (rec.is_deleted) continue;  // index tombstone race guard
    out = rec.tuple;
    return true;
  }
  return false;
}

// ---- Filter ----

bool Filter::next(Tuple& out) {
  Tuple t;
  while (child_->next(t)) {
    if (pred_.eval(t)) {
      out = std::move(t);
      return true;
    }
  }
  return false;
}

// ---- Projection ----

Projection::Projection(OperatorPtr child, std::vector<int> cols)
    : child_(std::move(child)), cols_(std::move(cols)) {
  const Schema& in = child_->schema();
  for (int c : cols_) out_schema_.push_back(in[c]);
}

bool Projection::next(Tuple& out) {
  Tuple t;
  if (!child_->next(t)) return false;
  out.clear();
  out.reserve(cols_.size());
  for (int c : cols_) out.push_back(t[c]);
  return true;
}

// ---- NestedLoopJoin ----

NestedLoopJoin::NestedLoopJoin(OperatorPtr outer, OperatorPtr inner,
                               int left_col, int right_col)
    : outer_(std::move(outer)),
      inner_(std::move(inner)),
      left_col_(left_col),
      right_col_(right_col) {
  out_schema_ = outer_->schema();
  const Schema& rs = inner_->schema();
  out_schema_.insert(out_schema_.end(), rs.begin(), rs.end());
}

void NestedLoopJoin::open() {
  outer_->open();
  inner_->open();
  have_outer_ = false;
}

bool NestedLoopJoin::next(Tuple& out) {
  while (true) {
    // Advance to the next outer tuple when needed; restart the inner scan.
    if (!have_outer_) {
      if (!outer_->next(outer_tuple_)) return false;  // outer exhausted -> done
      have_outer_ = true;
      inner_->close();
      inner_->open();
    }
    Tuple inner_tuple;
    if (inner_->next(inner_tuple)) {
      if (outer_tuple_[left_col_] == inner_tuple[right_col_]) {
        out = outer_tuple_;  // left-deep: outer columns first, then inner
        out.insert(out.end(), inner_tuple.begin(), inner_tuple.end());
        return true;
      }
      // No match on this pair; keep scanning the inner side.
    } else {
      have_outer_ = false;  // inner done for this outer -> fetch next outer
    }
  }
}

void NestedLoopJoin::close() {
  outer_->close();
  inner_->close();
}

// ---- Insert ----

void Insert::open() {
  ctx_.lock(table_->name(), LockMode::Exclusive);
  done_ = false;
  inserted_ = 0;
}

bool Insert::next(Tuple& /*out*/) {
  if (done_) return false;
  if (row_.size() != table_->schema().size()) {
    throw std::runtime_error("Insert: arity mismatch for table '" +
                             table_->name() + "'");
  }
  table_->insert(row_);
  ++inserted_;
  done_ = true;
  return false;  // DML produces no output rows
}

// ---- Delete ----

void Delete::open() {
  ctx_.lock(table_->name(), LockMode::Exclusive);
  child_->open();
  done_ = false;
  deleted_ = 0;
}

bool Delete::next(Tuple& /*out*/) {
  if (done_) return false;
  if (!tableHasIntPk(*table_)) {
    throw std::runtime_error("Delete requires an integer primary key on '" +
                             table_->name() + "'");
  }
  int pk = table_->pk_index();
  Tuple t;
  while (child_->next(t)) {
    auto rid = table_->index().search(t[pk].i);
    if (rid) {
      table_->markDeleted(*rid);
      ++deleted_;
    }
  }
  done_ = true;
  return false;  // DML produces no output rows
}

// ---- Driver ----

std::vector<Tuple> execute(Operator& root) {
  std::vector<Tuple> rows;
  root.open();
  Tuple t;
  while (root.next(t)) rows.push_back(t);
  root.close();
  return rows;
}

}  // namespace minidb
