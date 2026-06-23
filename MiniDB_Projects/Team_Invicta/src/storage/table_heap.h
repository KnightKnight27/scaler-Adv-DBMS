#pragma once
#include <string>
#include "common/types.h"
#include "storage/buffer_pool_manager.h"

namespace minidb {

// A TableHeap stores raw tuple bytes in a singly linked list of slotted pages
// (each page's next_page_id points to the next). Inserts walk the chain looking
// for free space and append a new page when none is found. RIDs returned are
// stable physical addresses suitable for indexing.
class TableHeap {
 public:
  // Open an existing heap rooted at `first_page_id`, or create a new one
  // (pass INVALID_PAGE_ID) — in which case `first_page_id` is updated to the
  // newly allocated first page.
  TableHeap(BufferPoolManager *bpm, page_id_t *first_page_id);

  // Insert serialized tuple bytes; returns its RID.
  RID InsertTuple(const std::string &bytes);

  // Read tuple bytes at `rid`. Returns false if missing/deleted.
  bool GetTuple(const RID &rid, std::string *out);

  // Tombstone the tuple at `rid`.
  bool DeleteTuple(const RID &rid);

  page_id_t first_page_id() const { return first_page_id_; }

  // Forward iterator over all live tuples for sequential scans.
  class Iterator {
   public:
    Iterator(TableHeap *heap, RID rid) : heap_(heap), rid_(rid) {}
    bool AtEnd() const { return rid_.page_id == INVALID_PAGE_ID; }
    const RID &rid() const { return rid_; }
    const std::string &value() const { return value_; }
    void Advance();  // move to next live tuple (or end)

   private:
    TableHeap  *heap_;
    RID         rid_;
    std::string value_;
    friend class TableHeap;
  };

  Iterator Begin();

 private:
  BufferPoolManager *bpm_;
  page_id_t          first_page_id_;
};

}  // namespace minidb
