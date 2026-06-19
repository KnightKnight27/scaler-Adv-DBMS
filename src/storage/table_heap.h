#pragma once

#include "buffer/buffer_pool_manager.h"
#include "storage/table_page.h"
#include "storage/tuple.h"

namespace minidb {

// A heap file: an unordered collection of tuples spread across a singly-linked
// list of slotted pages. New tuples are appended to the last page (a fresh page
// is linked on when it fills). Tuples are addressed by RID and scanned via the
// forward iterator below.
class TableHeap {
 public:
  TableHeap(BufferPoolManager *bpm, page_id_t first_page_id);

  // Allocate a fresh, empty heap and return its first page id.
  static page_id_t CreateNew(BufferPoolManager *bpm);

  page_id_t GetFirstPageId() const { return first_page_id_; }

  bool InsertTuple(const Tuple &t, RID *rid);
  bool GetTuple(const RID &rid, Tuple *out);
  bool MarkDelete(const RID &rid);

  // Read a slot's current byte offset (0 if absent/deleted), and restore it.
  // Used to undo a delete during transaction rollback: capture the offset before
  // MarkDelete, then write it back to make the tombstoned tuple live again.
  uint16_t PeekSlotOffset(const RID &rid);
  bool RestoreSlot(const RID &rid, uint16_t offset);

  // Forward iterator over all live (non-deleted) tuples.
  class Iterator {
   public:
    Iterator(TableHeap *heap, RID rid, bool end);
    bool operator!=(const Iterator &o) const;
    Iterator &operator++();
    Tuple operator*();
    RID GetRID() const { return rid_; }
    bool IsEnd() const { return end_; }

   private:
    void AdvanceToValid();  // park rid_ on the next live tuple, or set end_
    TableHeap *heap_;
    RID rid_;
    bool end_;
  };

  Iterator Begin();
  Iterator End();

 private:
  BufferPoolManager *bpm_;
  page_id_t first_page_id_;
  page_id_t last_page_id_;
};

}  // namespace minidb
