// execution.cpp — Phase A: page-backed executor (Volcano model)
#include "execution.h"

#include <cstring>
#include <fstream>
#include <stdexcept>

#include "slotted_page.h"
#include "transaction.h"

namespace minidb {

// Per-table buffer-pool sizes. Data is intentionally small so eviction is
// exercised during scans/large loads; the index pool is larger so a B+ Tree
// split never starves on pinned frames.
static constexpr int kDataPoolFrames = 64;
static constexpr int kIdxPoolFrames = 128;

// ---- Value ----

bool Value::operator==(const Value& o) const {
  if (type != o.type) return false;
  return type == ValueType::Int ? i == o.i : s == o.s;
}

bool Value::operator<(const Value& o) const {
  if (type != o.type) return type < o.type;
  return type == ValueType::Int ? i < o.i : s < o.s;
}

std::string Value::toString() const {
  return type == ValueType::Int ? std::to_string(i) : s;
}

// ---- Predicate / PredExpr ----

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

bool PredExpr::eval(const Tuple& t) const {
  switch (kind) {
    case Kind::Leaf: return leaf.eval(t);
    case Kind::And: return left->eval(t) && right->eval(t);  // short-circuit
    case Kind::Or: return left->eval(t) || right->eval(t);
  }
  return false;
}

// ---- Tuple serialization ----

std::vector<char> serializeTuple(const Tuple& t) {
  std::vector<char> out;
  for (const Value& v : t) {
    if (v.type == ValueType::Int) {
      out.push_back(0);
      const char* p = reinterpret_cast<const char*>(&v.i);
      out.insert(out.end(), p, p + 8);
    } else {
      out.push_back(1);
      uint32_t n = static_cast<uint32_t>(v.s.size());
      const char* pn = reinterpret_cast<const char*>(&n);
      out.insert(out.end(), pn, pn + 4);
      out.insert(out.end(), v.s.begin(), v.s.end());
    }
  }
  return out;
}

Tuple deserializeTuple(const char* data, size_t len, const Schema& schema) {
  (void)len;
  Tuple t;
  t.reserve(schema.size());
  size_t pos = 0;
  for (size_t c = 0; c < schema.size(); ++c) {
    uint8_t tag = static_cast<uint8_t>(data[pos++]);
    if (tag == 0) {
      int64_t x;
      std::memcpy(&x, data + pos, 8);
      pos += 8;
      t.push_back(Value::Int(x));
    } else {
      uint32_t n;
      std::memcpy(&n, data + pos, 4);
      pos += 4;
      t.push_back(Value::Text(std::string(data + pos, data + pos + n)));
      pos += n;
    }
  }
  return t;
}

// ---- Table (page-backed) ----

Table::Table(std::string name, Schema schema, int pk_index, bool fresh)
    : name_(std::move(name)), schema_(std::move(schema)), pk_index_(pk_index) {
  const std::string data_file = name_ + ".dat";
  if (fresh) std::ofstream(data_file, std::ios::binary | std::ios::trunc);
  data_heap_ = std::make_unique<HeapFile>(data_file);
  data_pool_ = std::make_unique<BufferPool>(kDataPoolFrames, data_heap_.get());
  num_pages_ = data_heap_->numPages();
  last_page_ = num_pages_ - 1;

  if (hasIntPk()) {
    const std::string idx_file = name_ + ".idx";
    if (fresh) std::ofstream(idx_file, std::ios::binary | std::ios::trunc);
    idx_heap_ = std::make_unique<HeapFile>(idx_file);
    idx_pool_ = std::make_unique<BufferPool>(kIdxPoolFrames, idx_heap_.get());
    pk_tree_.open(idx_pool_.get(), idx_heap_.get(), fresh);
  }

  if (!fresh) {  // reopened durable table: recount physical rows for size()
    for (int pg = 0; pg < num_pages_; ++pg)
      num_records_ += static_cast<size_t>(slotsInPage(pg));
  }
}

RID Table::insert(const Tuple& t) {
  std::vector<char> bytes = serializeTuple(t);
  uint16_t len = static_cast<uint16_t>(bytes.size());
  if (static_cast<int>(len) + SlottedPage::kSlotSize + SlottedPage::kHeaderSize >
      PAGE_SIZE)
    throw std::runtime_error("Table::insert: record too large for a page");

  int target = -1, slot = -1;
  if (last_page_ >= 0) {  // try the current last page first
    Page* p = data_pool_->getPage(last_page_);
    SlottedPage sp(p->data);
    slot = sp.insertRecord(bytes.data(), len);
    data_pool_->unpinPage(last_page_, slot >= 0);
    if (slot >= 0) target = last_page_;
  }
  if (slot < 0) {  // allocate a fresh page
    int pid = data_heap_->allocatePage();
    num_pages_ = pid + 1;
    last_page_ = pid;
    Page* p = data_pool_->getPage(pid);
    SlottedPage sp(p->data);
    slot = sp.insertRecord(bytes.data(), len);
    data_pool_->unpinPage(pid, true);
    target = pid;
  }

  RID rid{static_cast<uint32_t>(target), static_cast<uint16_t>(slot)};
  ++num_records_;
  if (hasIntPk()) pk_tree_.insert(t[pk_index_].i, rid);
  return rid;
}

bool Table::readRecord(RID rid, Tuple& out) const {
  Page* p = data_pool_->getPage(rid.page_id);
  if (!p) return false;
  SlottedPage sp(p->data);
  const char* rec = nullptr;
  uint16_t len = 0;
  bool ok = sp.getRecord(rid.slot_id, rec, len);
  bool live = ok && len > 0;
  if (live) out = deserializeTuple(rec, len, schema_);
  data_pool_->unpinPage(rid.page_id, false);
  return live;
}

void Table::markDeleted(RID rid) {
  Tuple t;
  bool live = readRecord(rid, t);  // capture key before tombstoning
  Page* p = data_pool_->getPage(rid.page_id);
  SlottedPage sp(p->data);
  sp.deleteRecord(rid.slot_id);
  data_pool_->unpinPage(rid.page_id, true);
  if (live && hasIntPk()) pk_tree_.remove(t[pk_index_].i);
}

uint16_t Table::slotLength(RID rid) const {
  Page* p = data_pool_->getPage(rid.page_id);
  SlottedPage sp(p->data);
  const char* rec = nullptr;
  uint16_t len = 0;
  sp.getRecord(rid.slot_id, rec, len);
  data_pool_->unpinPage(rid.page_id, false);
  return len;
}

void Table::restoreSlot(RID rid, uint16_t len) {
  Page* p = data_pool_->getPage(rid.page_id);
  SlottedPage sp(p->data);
  sp.restoreRecord(rid.slot_id, len);
  data_pool_->unpinPage(rid.page_id, true);
}

int Table::slotsInPage(int page_id) const {
  Page* p = data_pool_->getPage(page_id);
  SlottedPage sp(p->data);
  int n = sp.numSlots();
  data_pool_->unpinPage(page_id, false);
  return n;
}

void Table::flush() {
  data_pool_->checkpointFlush();
  if (idx_pool_) idx_pool_->checkpointFlush();
}

Table* Catalog::createTable(const std::string& name, Schema schema,
                            int pk_index, bool fresh) {
  auto tbl = std::make_unique<Table>(name, std::move(schema), pk_index, fresh);
  Table* raw = tbl.get();
  tables_[name] = std::move(tbl);
  return raw;
}

Table* Catalog::getTable(const std::string& name) {
  auto it = tables_.find(name);
  return it == tables_.end() ? nullptr : it->second.get();
}

void Catalog::checkpointAll() {
  for (auto& [name, tbl] : tables_) tbl->flush();
}

// ---- TableScan ----

void TableScan::open() {
  ctx_.lock(table_->name(), LockMode::Shared);
  page_ = 0;
  slot_ = 0;
  num_pages_ = table_->numPages();
}

bool TableScan::next(Tuple& out) {
  while (page_ < num_pages_) {
    int ns = table_->slotsInPage(page_);
    if (slot_ < ns) {
      RID rid{static_cast<uint32_t>(page_), static_cast<uint16_t>(slot_)};
      ++slot_;
      Tuple t;
      if (table_->readRecord(rid, t)) {  // skip tombstones
        out = std::move(t);
        return true;
      }
      continue;
    }
    ++page_;
    slot_ = 0;
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
    Tuple t;
    if (table_->readRecord(rid, t)) {  // index tombstone race guard
      out = std::move(t);
      return true;
    }
  }
  return false;
}

// ---- Filter ----

bool Filter::next(Tuple& out) {
  Tuple t;
  while (child_->next(t)) {
    if (expr_->eval(t)) {
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
    if (!have_outer_) {
      if (!outer_->next(outer_tuple_)) return false;
      have_outer_ = true;
      inner_->close();
      inner_->open();
    }
    Tuple inner_tuple;
    if (inner_->next(inner_tuple)) {
      if (outer_tuple_[left_col_] == inner_tuple[right_col_]) {
        out = outer_tuple_;
        out.insert(out.end(), inner_tuple.begin(), inner_tuple.end());
        return true;
      }
    } else {
      have_outer_ = false;
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
  if (row_.size() != table_->schema().size())
    throw std::runtime_error("Insert: arity mismatch for table '" +
                             table_->name() + "'");
  RID rid = table_->insert(row_);
  if (ctx_.txn_obj) {  // undo: tombstone the inserted row on abort
    Table* t = table_;
    ctx_.txn_obj->addUndo([t, rid] { t->markDeleted(rid); });
  }
  ++inserted_;
  done_ = true;
  return false;
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
  if (!table_->hasIntPk())
    throw std::runtime_error("Delete requires an integer primary key on '" +
                             table_->name() + "'");
  int pk = table_->pk_index();
  Tuple t;
  while (child_->next(t)) {
    Key key = t[pk].i;
    auto rid = table_->index().search(key);
    if (rid) {
      uint16_t len = table_->slotLength(*rid);
      RID r = *rid;
      table_->markDeleted(r);
      if (ctx_.txn_obj) {  // undo: restore the row + its index entry on abort
        Table* tb = table_;
        ctx_.txn_obj->addUndo([tb, r, len, key] {
          tb->restoreSlot(r, len);
          tb->index().insert(key, r);
        });
      }
      ++deleted_;
    }
  }
  done_ = true;
  return false;
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
