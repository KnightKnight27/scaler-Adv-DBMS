// ============================================================================
// table_heap.h  --  A table's data stored as a HEAP FILE: an unordered set of
// tuples spread across a singly-linked list of slotted pages.
//
//   first_page ──next──> page ──next──> page ──next──> (INVALID = end)
//
// "Heap" means tuples are not kept in any particular order -- new rows go into
// whatever page has room.  Ordered access is the job of the B+ Tree index, not
// the heap.  The heap supports exactly what the executor needs:
//   * insertTuple  -> returns the new tuple's RID
//   * getTuple     -> bytes at a RID
//   * markDelete   -> tombstone a RID
//   * an iterator over every live tuple (used by sequential scans)
//
// TableHeap is pure storage: it knows nothing about transactions or the WAL.
// The execution layer logs to the WAL *before* calling these methods.
// ============================================================================
#pragma once

#include "common/common.h"
#include "storage/buffer_pool.h"
#include "storage/table_page.h"

namespace minidb {

class TableHeap {
 public:
  // Open an existing heap whose first page is `first_page_id`, or create a new
  // one if first_page_id == INVALID_PAGE_ID (the new id is exposed via first()).
  TableHeap(BufferPool *bpm, page_id_t first_page_id);

  page_id_t first() const { return first_page_id_; }

  // insert serialized tuple bytes; returns the RID where it landed.
  RID insertTuple(const string &bytes);

  // Read the tuple at `rid`.  Returns false if it is deleted or absent.
  bool getTuple(const RID &rid, string *out);

  // Tombstone the tuple at `rid`.
  bool markDelete(const RID &rid);

  // Recovery entry points (place/restore a tuple at an exact RID; idempotent).
  void recoverInsert(const RID &rid, const string &bytes);
  void recoverDelete(const RID &rid);

  // ---- Forward iterator over all *live* tuples in the heap ----
  class Iterator {
   public:
    Iterator(TableHeap *heap, RID rid) : heap_(heap), rid_(rid) {}
    bool operator!=(const Iterator &o) const { return rid_ != o.rid_; }
    const RID &rid() const { return rid_; }
    const string &bytes() const { return bytes_; }
    void advance();   // move to next live tuple (or to end)
   private:
    TableHeap  *heap_;
    RID         rid_;
    string bytes_;
    friend class TableHeap;
  };

  Iterator begin();
  Iterator end() { return Iterator(this, RID(INVALID_PAGE_ID, -1)); }

 private:
  // Ensure the page at `page_id` exists in a way we can write tuples into, and
  // return the id of the last page in the chain (where we try to append).
  page_id_t lastPage();

  BufferPool *bpm_;
  page_id_t   first_page_id_;
};

}  // namespace minidb
