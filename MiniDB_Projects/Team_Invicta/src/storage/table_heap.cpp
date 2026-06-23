#include "storage/table_heap.h"
#include <stdexcept>
#include "storage/table_page.h"

namespace minidb {

TableHeap::TableHeap(BufferPoolManager *bpm, page_id_t *first_page_id)
    : bpm_(bpm), first_page_id_(*first_page_id) {
  if (first_page_id_ == INVALID_PAGE_ID) {
    // Create the first page of a new heap.
    page_id_t pid;
    Page *page = bpm_->NewPage(&pid);
    if (!page) throw std::runtime_error("TableHeap: no frame for first page");
    TablePage(page).Init();
    bpm_->UnpinPage(pid, true);
    first_page_id_ = pid;
    *first_page_id = pid;
  }
}

RID TableHeap::InsertTuple(const std::string &bytes) {
  page_id_t pid = first_page_id_;
  while (true) {
    Page *page = bpm_->FetchPage(pid);
    if (!page) throw std::runtime_error("TableHeap: cannot fetch page");
    TablePage tp(page);
    slot_id_t slot;
    if (tp.InsertTuple(bytes, &slot)) {
      bpm_->UnpinPage(pid, true);
      return RID{pid, slot};
    }
    // Page full: follow the chain, or append a new page at the tail.
    page_id_t next = tp.GetNextPageId();
    if (next == INVALID_PAGE_ID) {
      page_id_t new_pid;
      Page *new_page = bpm_->NewPage(&new_pid);
      if (!new_page) throw std::runtime_error("TableHeap: no frame for new page");
      TablePage(new_page).Init();
      bpm_->UnpinPage(new_pid, true);
      tp.SetNextPageId(new_pid);
      bpm_->UnpinPage(pid, true);
      pid = new_pid;
    } else {
      bpm_->UnpinPage(pid, false);
      pid = next;
    }
  }
}

bool TableHeap::GetTuple(const RID &rid, std::string *out) {
  Page *page = bpm_->FetchPage(rid.page_id);
  if (!page) return false;
  bool ok = TablePage(page).GetTuple(rid.slot, out);
  bpm_->UnpinPage(rid.page_id, false);
  return ok;
}

bool TableHeap::DeleteTuple(const RID &rid) {
  Page *page = bpm_->FetchPage(rid.page_id);
  if (!page) return false;
  bool ok = TablePage(page).DeleteTuple(rid.slot);
  bpm_->UnpinPage(rid.page_id, ok);
  return ok;
}

void TableHeap::Iterator::Advance() {
  // Advance to the next live (non-tombstone) tuple, crossing pages as needed.
  while (rid_.page_id != INVALID_PAGE_ID) {
    Page *page = heap_->bpm_->FetchPage(rid_.page_id);
    TablePage tp(page);
    uint16_t num = tp.GetNumSlots();
    slot_id_t s = rid_.slot + 1;
    for (; s < static_cast<slot_id_t>(num); ++s) {
      if (tp.GetTuple(s, &value_)) {
        rid_.slot = s;
        heap_->bpm_->UnpinPage(rid_.page_id, false);
        return;
      }
    }
    page_id_t next = tp.GetNextPageId();
    heap_->bpm_->UnpinPage(rid_.page_id, false);
    rid_ = RID{next, -1};  // restart slot search on the next page
  }
}

TableHeap::Iterator TableHeap::Begin() {
  Iterator it(this, RID{first_page_id_, -1});
  it.Advance();  // position at the first live tuple
  return it;
}

}  // namespace minidb
