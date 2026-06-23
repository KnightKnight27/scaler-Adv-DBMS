#include "minidb/storage/heap_engine.hpp"

#include "minidb/serialize.hpp"
#include <algorithm>
#include <stdexcept>

namespace minidb {

HeapEngine::HeapEngine(Catalog& catalog, const std::string& data_path, size_t buffer_frames)
    : catalog_(catalog), pm_(data_path), pool_(pm_, buffer_frames) {
  rebuild_all_indexes();
}

void HeapEngine::rebuild_all_indexes() {
  indexes_.clear();
  for (const IndexMeta& ix : catalog_.indexes()) build_index(ix.id);
}

void HeapEngine::build_index(IndexId id) {
  // Find the index metadata.
  const IndexMeta* meta = nullptr;
  for (const IndexMeta& ix : catalog_.indexes())
    if (ix.id == id) { meta = &ix; break; }
  if (!meta) return;
  BPlusTree& tree = indexes_[id];
  auto iter = scan(meta->table);
  RID rid;
  Tuple t;
  while (iter->next(rid, t)) tree.insert(t[meta->column], rid);
}

const BPlusTree* HeapEngine::index_for(TableId table, size_t column) const {
  for (const IndexMeta& ix : catalog_.indexes()) {
    if (ix.table == table && ix.column == column) {
      auto it = indexes_.find(ix.id);
      if (it != indexes_.end()) return &it->second;
    }
  }
  return nullptr;
}

void HeapEngine::index_insert(const TableMeta& meta, const Tuple& t, const RID& rid) {
  for (const IndexMeta* ix : catalog_.indexes_for(meta.id))
    indexes_[ix->id].insert(t[ix->column], rid);
}

void HeapEngine::index_remove(const TableMeta& meta, const Tuple& t, const RID& rid) {
  for (const IndexMeta* ix : catalog_.indexes_for(meta.id))
    indexes_[ix->id].remove(t[ix->column], rid);
}

RID HeapEngine::insert(TableId table, const Tuple& t) {
  TableMeta& meta = catalog_.by_id(table);
  std::vector<uint8_t> bytes = serialize_tuple(meta.schema, t);
  if (bytes.size() > kPageSize - 8)
    throw std::runtime_error("record too large for a page");

  // Try the last page first (records are appended).
  if (!meta.data_pages.empty()) {
    PageId pid = meta.data_pages.back();
    uint8_t* buf = pool_.fetch_page(pid);
    SlottedPage page(buf);
    uint16_t slot = page.insert(bytes.data(), static_cast<uint16_t>(bytes.size()));
    if (slot != SlottedPage::kInvalidSlot) {
      pool_.unpin(pid, /*dirty=*/true);
      RID rid{pid, slot};
      index_insert(meta, t, rid);
      return rid;
    }
    pool_.unpin(pid, /*dirty=*/false);
  }

  // Allocate a fresh page.
  uint8_t* buf = nullptr;
  PageId pid = pool_.new_page(buf);
  SlottedPage page(buf);
  page.init();
  uint16_t slot = page.insert(bytes.data(), static_cast<uint16_t>(bytes.size()));
  pool_.unpin(pid, /*dirty=*/true);
  meta.data_pages.push_back(pid);
  if (slot == SlottedPage::kInvalidSlot) throw std::runtime_error("record did not fit in a fresh page");
  RID rid{pid, slot};
  index_insert(meta, t, rid);
  return rid;
}

Optional<Tuple> HeapEngine::get(TableId table, const RID& rid) {
  TableMeta& meta = catalog_.by_id(table);
  uint8_t* buf = pool_.fetch_page(rid.page_id);
  SlottedPage page(buf);
  const uint8_t* rec = nullptr;
  uint16_t len = 0;
  Optional<Tuple> result;
  if (page.get(rid.slot, rec, len)) result = deserialize_tuple(meta.schema, rec, len);
  pool_.unpin(rid.page_id, /*dirty=*/false);
  return result;
}

Optional<Tuple> HeapEngine::find(TableId table, const Value& key) {
  TableMeta& meta = catalog_.by_id(table);
  size_t pk = meta.schema.primary_key_index();
  if (pk == Schema::npos) return Optional<Tuple>();
  const BPlusTree* tree = index_for(table, pk);
  if (tree) {
    std::vector<RID> rids = tree->find(key);
    if (rids.empty()) return Optional<Tuple>();
    return get(table, rids.front());
  }
  // No index on the primary key: linear scan fallback.
  auto iter = scan(table);
  RID rid;
  Tuple t;
  while (iter->next(rid, t))
    if (t[pk] == key) return Optional<Tuple>(t);
  return Optional<Tuple>();
}

std::vector<RID> HeapEngine::index_lookup(TableId table, size_t column, const Value& key) {
  const BPlusTree* tree = index_for(table, column);
  if (!tree) return {};
  return tree->find(key);
}

bool HeapEngine::remove(TableId table, const RID& rid) {
  TableMeta& meta = catalog_.by_id(table);
  Optional<Tuple> tuple = get(table, rid);
  uint8_t* buf = pool_.fetch_page(rid.page_id);
  SlottedPage page(buf);
  bool ok = page.remove(rid.slot);
  pool_.unpin(rid.page_id, /*dirty=*/ok);
  if (ok && tuple) index_remove(meta, *tuple, rid);
  return ok;
}

namespace {
// Eagerly materializes a table's live records. Simple and pin-safe for v1;
// a lazy page-at-a-time cursor can replace it later.
class HeapIterator : public RecordIterator {
 public:
  explicit HeapIterator(std::vector<std::pair<RID, Tuple>> rows) : rows_(std::move(rows)) {}
  bool next(RID& rid, Tuple& out) override {
    if (pos_ >= rows_.size()) return false;
    rid = rows_[pos_].first;
    out = rows_[pos_].second;
    ++pos_;
    return true;
  }

 private:
  std::vector<std::pair<RID, Tuple>> rows_;
  size_t pos_ = 0;
};
}  // namespace

std::unique_ptr<RecordIterator> HeapEngine::scan(TableId table) {
  TableMeta& meta = catalog_.by_id(table);
  std::vector<std::pair<RID, Tuple>> rows;
  for (PageId pid : meta.data_pages) {
    uint8_t* buf = pool_.fetch_page(pid);
    SlottedPage page(buf);
    uint16_t n = page.num_slots();
    for (uint16_t s = 0; s < n; ++s) {
      const uint8_t* rec = nullptr;
      uint16_t len = 0;
      if (page.get(s, rec, len))
        rows.emplace_back(RID{pid, s}, deserialize_tuple(meta.schema, rec, len));
    }
    pool_.unpin(pid, /*dirty=*/false);
  }
  return std::make_unique<HeapIterator>(std::move(rows));
}

void HeapEngine::replay_insert(TableId table, const RID& rid,
                               const std::vector<uint8_t>& tuple_bytes) {
  TableMeta& meta = catalog_.by_id(table);
  // Ensure the page physically exists (it was allocated, hence persisted, when
  // the record was first written) and is registered with the table.
  while (pm_.num_pages() <= rid.page_id) pm_.allocate_page();
  if (std::find(meta.data_pages.begin(), meta.data_pages.end(), rid.page_id) ==
      meta.data_pages.end())
    meta.data_pages.push_back(rid.page_id);

  uint8_t* buf = pool_.fetch_page(rid.page_id);
  SlottedPage page(buf);
  if (!page.is_initialized()) page.init();  // page reconstructed from a zeroed disk page
  page.insert_at(rid.slot, tuple_bytes.data(), static_cast<uint16_t>(tuple_bytes.size()));
  pool_.unpin(rid.page_id, /*dirty=*/true);

  Tuple t = deserialize_tuple(meta.schema, tuple_bytes.data(), tuple_bytes.size());
  index_insert(meta, t, rid);
}

void HeapEngine::replay_delete(TableId table, const RID& rid) {
  TableMeta& meta = catalog_.by_id(table);
  // Only undo/redo against pages the table actually owns; an unknown page id
  // means the row never became visible (orphaned allocation), so skip.
  if (rid.page_id >= pm_.num_pages()) return;
  if (std::find(meta.data_pages.begin(), meta.data_pages.end(), rid.page_id) ==
      meta.data_pages.end())
    return;
  Optional<Tuple> tuple = get(table, rid);
  uint8_t* buf = pool_.fetch_page(rid.page_id);
  SlottedPage page(buf);
  bool ok = page.remove(rid.slot);
  pool_.unpin(rid.page_id, /*dirty=*/ok);
  if (ok && tuple) index_remove(meta, *tuple, rid);
}

void HeapEngine::flush() { pool_.flush_all(); }

}  // namespace minidb
