#include "storage/row_store.h"
#include <limits>

namespace minidb {

// A cursor over a materialized list of rows (used for range scans and LSM).
class VectorCursor : public RowCursor {
 public:
  explicit VectorCursor(std::vector<std::string> rows) : rows_(std::move(rows)) {}
  bool Next(std::string* row) override {
    if (i_ >= rows_.size()) return false;
    *row = std::move(rows_[i_++]);
    return true;
  }
 private:
  std::vector<std::string> rows_;
  size_t i_ = 0;
};

// A streaming cursor over a heap's page chain (no full materialization).
class HeapCursor : public RowCursor {
 public:
  explicit HeapCursor(TableHeap* heap) : it_(heap->Begin()) {}
  bool Next(std::string* row) override {
    if (it_.IsEnd()) return false;
    *row = it_.Data();
    it_.Next();
    return true;
  }
 private:
  TableHeap::Iterator it_;
};

// ---- HeapRowStore ----
std::unique_ptr<RowCursor> HeapRowStore::FullScan() {
  return std::make_unique<HeapCursor>(heap_.get());
}

std::unique_ptr<RowCursor> HeapRowStore::RangeScan(int64_t low, int64_t high) {
  std::vector<std::string> rows;
  if (index_) {
    for (auto& [key, rid] : index_->Range(low, high)) {
      (void)key;
      std::string row;
      if (heap_->GetTuple(rid, &row)) rows.push_back(std::move(row));
    }
  }
  return std::make_unique<VectorCursor>(std::move(rows));
}

bool HeapRowStore::Point(int64_t key, std::string* row) {
  if (!index_) return false;
  RID rid;
  if (!index_->Search(key, &rid)) return false;
  return heap_->GetTuple(rid, row);
}

void HeapRowStore::Insert(int64_t key, const std::string& row) {
  RID rid = heap_->InsertTuple(row);
  if (index_) index_->Insert(key, rid);
}

void HeapRowStore::Delete(int64_t key) {
  if (!index_) return;
  RID rid;
  if (!index_->Search(key, &rid)) return;
  heap_->DeleteTuple(rid);
  index_->Delete(key);
}

// ---- LsmRowStore ----
std::unique_ptr<RowCursor> LsmRowStore::FullScan() {
  std::vector<std::string> rows;
  for (auto& [key, val] : lsm_->Scan(std::numeric_limits<int64_t>::min(),
                                     std::numeric_limits<int64_t>::max())) {
    (void)key;
    rows.push_back(val);
  }
  return std::make_unique<VectorCursor>(std::move(rows));
}

std::unique_ptr<RowCursor> LsmRowStore::RangeScan(int64_t low, int64_t high) {
  std::vector<std::string> rows;
  for (auto& [key, val] : lsm_->Scan(low, high)) { (void)key; rows.push_back(val); }
  return std::make_unique<VectorCursor>(std::move(rows));
}

bool LsmRowStore::Point(int64_t key, std::string* row) { return lsm_->Get(key, row); }
void LsmRowStore::Insert(int64_t key, const std::string& row) { lsm_->Put(key, row); }
void LsmRowStore::Delete(int64_t key) { lsm_->Delete(key); }
void LsmRowStore::Sync() { lsm_->Flush(); }

}  // namespace minidb
