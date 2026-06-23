#include "storage/heap_table.h"
#include <cstring>
#include "storage/slotted_page.h"

namespace minidb {
namespace {

// Heap record = 8-byte key prefix + serialized row value.
constexpr std::size_t kKeyPrefix = sizeof(Key);

std::vector<char> encode_record(Key key, const Bytes& value) {
  std::vector<char> rec(kKeyPrefix + value.size());
  std::memcpy(rec.data(), &key, kKeyPrefix);
  std::memcpy(rec.data() + kKeyPrefix, value.data(), value.size());
  return rec;
}

Key decode_key(const std::vector<char>& rec) {
  Key key;
  std::memcpy(&key, rec.data(), kKeyPrefix);
  return key;
}

Bytes decode_value(const std::vector<char>& rec) {
  return Bytes(rec.begin() + kKeyPrefix, rec.end());
}

// Cursor over a heap file, splitting each record back into (key, value).
class HeapScanCursor : public RowCursor {
 public:
  explicit HeapScanCursor(HeapFile::Cursor cur) : cur_(std::move(cur)) {}
  bool next(Key& out_key, Bytes& out_value) override {
    RID rid;
    std::vector<char> rec;
    if (!cur_.next(rid, rec)) return false;
    out_key   = decode_key(rec);
    out_value = decode_value(rec);
    return true;
  }
 private:
  HeapFile::Cursor cur_;
};

// Cursor over an already-materialized list of (key, value) pairs.
class VectorCursor : public RowCursor {
 public:
  explicit VectorCursor(std::vector<std::pair<Key, Bytes>> rows) : rows_(std::move(rows)) {}
  bool next(Key& out_key, Bytes& out_value) override {
    if (pos_ >= rows_.size()) return false;
    out_key   = rows_[pos_].first;
    out_value = rows_[pos_].second;
    ++pos_;
    return true;
  }
 private:
  std::vector<std::pair<Key, Bytes>> rows_;
  std::size_t pos_ = 0;
};

}  // namespace

HeapTable::HeapTable(BufferPool& buffer_pool, PageId first_page)
    : buffer_pool_(buffer_pool), heap_(buffer_pool, first_page) {
  rebuild_index();
}

void HeapTable::note_key(Key key) {
  if (stats_.row_count == 0) {
    stats_.min_key = stats_.max_key = key;
  } else {
    if (key < stats_.min_key) stats_.min_key = key;
    if (key > stats_.max_key) stats_.max_key = key;
  }
}

void HeapTable::rebuild_index() {
  index_.clear();
  stats_ = TableStats{};
  stats_.has_index = true;
  HeapFile::Cursor cur = heap_.scan();
  RID rid;
  std::vector<char> rec;
  while (cur.next(rid, rec)) {
    Key key = decode_key(rec);
    index_.insert(key, rid);
    note_key(key);
    stats_.row_count += 1;
  }
}

void HeapTable::insert(Key key, const Bytes& value) {
  if (index_.search(key)) erase(key);  // upsert: replace any existing row
  RID rid = heap_.insert(encode_record(key, value));
  index_.insert(key, rid);
  note_key(key);
  stats_.row_count += 1;
}

std::optional<Bytes> HeapTable::get(Key key) {
  std::optional<RID> rid = index_.search(key);
  if (!rid) return std::nullopt;
  std::vector<char> rec = heap_.get(*rid);
  if (rec.empty()) return std::nullopt;
  return decode_value(rec);
}

void HeapTable::erase(Key key) {
  std::optional<RID> rid = index_.search(key);
  if (!rid) return;
  heap_.erase(*rid);
  index_.erase(key);
  if (stats_.row_count > 0) stats_.row_count -= 1;
}

std::unique_ptr<RowCursor> HeapTable::scan() {
  return std::make_unique<HeapScanCursor>(heap_.scan());
}

std::unique_ptr<RowCursor> HeapTable::index_range(Key lo, Key hi) {
  std::vector<std::pair<Key, Bytes>> rows;
  for (const auto& [key, rid] : index_.range(lo, hi)) {
    std::vector<char> rec = heap_.get(rid);
    if (!rec.empty()) rows.emplace_back(key, decode_value(rec));
  }
  return std::make_unique<VectorCursor>(std::move(rows));
}

std::unique_ptr<HeapTable> make_heap_table(BufferPool& buffer_pool) {
  PageId first = buffer_pool.allocate_page();
  Page* page = buffer_pool.fetch_page(first);
  SlottedPage(page).init();
  buffer_pool.unpin_page(first, true);
  // Persist the empty page so a reopened table has a valid first page even if a
  // crash later drops the unflushed data pages (needed by crash recovery).
  buffer_pool.flush_page(first);
  return std::make_unique<HeapTable>(buffer_pool, first);
}

}  // namespace minidb
