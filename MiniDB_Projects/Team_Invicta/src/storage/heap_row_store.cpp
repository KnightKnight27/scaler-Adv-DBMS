#include "storage/heap_row_store.h"
#include <stdexcept>

namespace minidb {

HeapRowStore::HeapRowStore(BufferPoolManager *bpm, Schema schema,
                           page_id_t *heap_first, page_id_t *index_root)
    : bpm_(bpm), schema_(std::move(schema)) {
  heap_ = std::make_unique<TableHeap>(bpm_, heap_first);
  index_ = std::make_unique<BPlusTree>(bpm_, index_root);
  // Recover the live row count and key range by reading the index (sorted).
  auto all = index_->Range(INT64_MIN, INT64_MAX);
  row_count_ = all.size();
  if (!all.empty()) {
    has_keys_ = true;
    min_key_ = all.front().first;
    max_key_ = all.back().first;
  }
}

void HeapRowStore::Insert(int64_t key, const Tuple &row) {
  RID rid = heap_->InsertTuple(row.Serialize(schema_));
  if (!index_->Insert(key, rid)) {
    heap_->DeleteTuple(rid);  // roll back the heap write
    throw std::runtime_error("duplicate primary key: " + std::to_string(key));
  }
  ++row_count_;
  if (!has_keys_) { has_keys_ = true; min_key_ = max_key_ = key; }
  else { if (key < min_key_) min_key_ = key; if (key > max_key_) max_key_ = key; }
}

bool HeapRowStore::KeyRange(int64_t *min_key, int64_t *max_key) {
  if (!has_keys_ || row_count_ == 0) return false;
  *min_key = min_key_;
  *max_key = max_key_;
  return true;
}

bool HeapRowStore::Get(int64_t key, Tuple *out) {
  RID rid;
  if (!index_->GetValue(key, &rid)) return false;
  std::string bytes;
  if (!heap_->GetTuple(rid, &bytes)) return false;
  *out = Tuple::Deserialize(schema_, bytes);
  return true;
}

bool HeapRowStore::Delete(int64_t key) {
  RID rid;
  if (!index_->GetValue(key, &rid)) return false;
  index_->Delete(key);
  heap_->DeleteTuple(rid);
  if (row_count_ > 0) --row_count_;
  return true;
}

namespace {
// Cursor over the heap's sequential scan.
class HeapScanCursor : public RowCursor {
 public:
  HeapScanCursor(TableHeap *heap, const Schema &schema)
      : iter_(heap->Begin()), schema_(schema) {}
  bool Next(Tuple *out) override {
    if (iter_.AtEnd()) return false;
    *out = Tuple::Deserialize(schema_, iter_.value());
    iter_.Advance();
    return true;
  }

 private:
  TableHeap::Iterator iter_;
  const Schema       &schema_;
};

// Cursor over a B+ tree range: resolve the matching RIDs up front, then fetch
// and decode each tuple lazily from the heap.
class IndexRangeCursor : public RowCursor {
 public:
  IndexRangeCursor(TableHeap *heap, const Schema &schema,
                   std::vector<std::pair<int64_t, RID>> hits)
      : heap_(heap), schema_(schema), hits_(std::move(hits)) {}
  bool Next(Tuple *out) override {
    while (pos_ < hits_.size()) {
      std::string bytes;
      RID rid = hits_[pos_++].second;
      if (heap_->GetTuple(rid, &bytes)) {
        *out = Tuple::Deserialize(schema_, bytes);
        return true;
      }
    }
    return false;
  }

 private:
  TableHeap                           *heap_;
  const Schema                        &schema_;
  std::vector<std::pair<int64_t, RID>> hits_;
  size_t                               pos_{0};
};
}  // namespace

std::unique_ptr<RowCursor> HeapRowStore::ScanAll() {
  return std::make_unique<HeapScanCursor>(heap_.get(), schema_);
}

std::unique_ptr<RowCursor> HeapRowStore::RangeScan(int64_t low, int64_t high) {
  return std::make_unique<IndexRangeCursor>(heap_.get(), schema_,
                                            index_->Range(low, high));
}

}  // namespace minidb
