#pragma once
#include <memory>
#include "index/bplus_tree.h"
#include "storage/buffer_pool_manager.h"
#include "storage/row_store.h"
#include "storage/table_heap.h"

namespace minidb {

// The classic row store: tuples live in a slotted-page heap, and a B+ tree maps
// each primary key to the tuple's RID. Point/range lookups use the index; full
// scans walk the heap. This is the baseline that Track C (LSM) is compared to.
class HeapRowStore : public RowStore {
 public:
  // `heap_first` / `index_root` are in/out: pass INVALID_PAGE_ID to create, or
  // existing ids to reopen. They are updated so the engine can persist them in
  // the catalog.
  HeapRowStore(BufferPoolManager *bpm, Schema schema, page_id_t *heap_first,
               page_id_t *index_root);

  void Insert(int64_t key, const Tuple &row) override;
  bool Get(int64_t key, Tuple *out) override;
  bool Delete(int64_t key) override;
  std::unique_ptr<RowCursor> ScanAll() override;
  std::unique_ptr<RowCursor> RangeScan(int64_t low, int64_t high) override;
  size_t RowCount() override { return row_count_; }
  bool KeyRange(int64_t *min_key, int64_t *max_key) override;
  void Sync() override { bpm_->FlushAll(); }  // make all dirty pages durable
  const Schema &schema() const override { return schema_; }

 private:
  BufferPoolManager         *bpm_;
  Schema                     schema_;
  std::unique_ptr<TableHeap> heap_;
  std::unique_ptr<BPlusTree> index_;
  size_t                     row_count_{0};
  int64_t                    min_key_{0};
  int64_t                    max_key_{0};
  bool                       has_keys_{false};
};

}  // namespace minidb
