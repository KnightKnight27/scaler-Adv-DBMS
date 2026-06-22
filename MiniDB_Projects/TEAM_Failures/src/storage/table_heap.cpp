#include "storage/table_heap.h"

namespace minidb {

// We persist the heap's *structure* (page allocations and next-page links)
// eagerly by flushing those pages immediately.  Tuple *contents* are not forced
// -- they are protected by the WAL and replayed by recovery.  This split keeps
// the page chain walkable after a crash while still letting us demonstrate REDO.

TableHeap::TableHeap(BufferPool *bpm, page_id_t first_page_id)
    : bpm_(bpm), first_page_id_(first_page_id) {
  if (first_page_id_ == INVALID_PAGE_ID) {
    page_id_t pid;
    Page *p = bpm_->newPage(&pid);
    TablePage(p).init();
    bpm_->unpinPage(pid, true);
    bpm_->flushPage(pid);        // make the new page durable right away
    first_page_id_ = pid;
  }
}

page_id_t TableHeap::lastPage() {
  page_id_t cur = first_page_id_;
  while (true) {
    Page *p = bpm_->fetchPage(cur);
    page_id_t next = TablePage(p).getNextPageId();
    bpm_->unpinPage(cur, false);
    if (next == INVALID_PAGE_ID) return cur;
    cur = next;
  }
}

RID TableHeap::insertTuple(const string &bytes) {
  if (static_cast<int>(bytes.size()) + TablePage::SLOT_SIZE >
      PAGE_SIZE - TablePage::HEADER_SIZE) {
    throw StorageError("tuple too large to fit in a page");
  }

  page_id_t cur = lastPage();
  Page *p = bpm_->fetchPage(cur);
  TablePage tp(p);
  int slot;
  if (tp.insertTuple(bytes, &slot)) {
    bpm_->unpinPage(cur, true);
    return RID(cur, slot);
  }
  // Current page is full: allocate and link a new page, then insert there.
  page_id_t new_pid;
  Page *np = bpm_->newPage(&new_pid);
  TablePage ntp(np);
  ntp.init();
  ntp.insertTuple(bytes, &slot);
  bpm_->unpinPage(new_pid, true);
  bpm_->flushPage(new_pid);           // persist the new page's structure

  tp.setNextPageId(new_pid);          // link previous -> new
  bpm_->unpinPage(cur, true);
  bpm_->flushPage(cur);               // persist the link too
  return RID(new_pid, slot);
}

bool TableHeap::getTuple(const RID &rid, string *out) {
  Page *p = bpm_->fetchPage(rid.page_id);
  if (p == nullptr) return false;
  bool ok = TablePage(p).getTuple(rid.slot, out);
  bpm_->unpinPage(rid.page_id, false);
  return ok;
}

bool TableHeap::markDelete(const RID &rid) {
  Page *p = bpm_->fetchPage(rid.page_id);
  if (p == nullptr) return false;
  bool ok = TablePage(p).deleteTuple(rid.slot);
  bpm_->unpinPage(rid.page_id, ok);
  return ok;
}

void TableHeap::recoverInsert(const RID &rid, const string &bytes) {
  Page *p = bpm_->fetchPage(rid.page_id);
  TablePage tp(p);
  if (tp.isUninitialized()) tp.init();   // crash may have caught an empty page
  tp.setTupleAtSlot(rid.slot, bytes);
  bpm_->unpinPage(rid.page_id, true);
}

void TableHeap::recoverDelete(const RID &rid) {
  Page *p = bpm_->fetchPage(rid.page_id);
  TablePage(p).deleteTuple(rid.slot);
  bpm_->unpinPage(rid.page_id, true);
}

// ------------------------------- Iterator ----------------------------------
void TableHeap::Iterator::advance() {
  // Move forward from the current (page, slot) to the next *live* tuple,
  // crossing page boundaries via the next-page link.  Stops at end().
  page_id_t pid = rid_.page_id;
  int slot = rid_.slot;
  while (pid != INVALID_PAGE_ID) {
    Page *p = heap_->bpm_->fetchPage(pid);
    TablePage tp(p);
    int num_slots = tp.getNumSlots();
    for (int s = slot + 1; s < num_slots; ++s) {
      string bytes;
      if (tp.getTuple(s, &bytes)) {
        rid_ = RID(pid, s);
        bytes_ = move(bytes);
        heap_->bpm_->unpinPage(pid, false);
        return;
      }
    }
    page_id_t next = tp.getNextPageId();
    heap_->bpm_->unpinPage(pid, false);
    pid = next;
    slot = -1;  // start from slot 0 on the next page
  }
  rid_ = RID(INVALID_PAGE_ID, -1);  // reached the end
}

TableHeap::Iterator TableHeap::begin() {
  // Start "before" the first slot of the first page, then advance to the first
  // live tuple.  This reuses the cross-page scanning logic in one place.
  Iterator it(this, RID(first_page_id_, -1));
  it.advance();
  return it;
}

}  // namespace minidb
