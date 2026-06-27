#pragma once
#include <string>
#include "common/types.h"
#include "storage/buffer_pool_manager.h"

namespace minidb {

// A heap file: a singly-linked chain of slotted pages holding a table's rows.
// `first_page_id` is recorded in the catalog so the table can be reopened.
class TableHeap {
 public:
  TableHeap(BufferPoolManager* bpm, page_id_t first_page_id)
      : bpm_(bpm), first_page_id_(first_page_id) {}

  // Create a fresh, empty heap and return its first page id.
  static page_id_t Create(BufferPoolManager* bpm);

  page_id_t FirstPageId() const { return first_page_id_; }

  // Insert serialized tuple bytes; returns the RID it was placed at.
  RID InsertTuple(const std::string& data);

  // Fetch tuple bytes at `rid`. Returns false if absent/deleted.
  bool GetTuple(const RID& rid, std::string* out);

  // Logical delete (tombstone) at `rid`.
  bool DeleteTuple(const RID& rid);

  // ---- Forward iterator over live tuples (used by sequential scan) ----
  class Iterator {
   public:
    Iterator(TableHeap* heap, RID rid) : heap_(heap), rid_(rid) { AdvanceToLive(); }
    bool IsEnd() const { return rid_.page_id == INVALID_PAGE_ID; }
    const RID& GetRID() const { return rid_; }
    const std::string& Data() const { return data_; }
    void Next() {
      rid_.slot_id++;
      AdvanceToLive();
    }

   private:
    void AdvanceToLive();
    TableHeap* heap_;
    RID rid_;
    std::string data_;
  };

  Iterator Begin() { return Iterator(this, RID{first_page_id_, 0}); }

 private:
  BufferPoolManager* bpm_;
  page_id_t first_page_id_;
};

}  // namespace minidb
