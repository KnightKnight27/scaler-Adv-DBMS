#include "storage/table_heap.h"
#include "storage/table_page.h"

namespace minidb {

page_id_t TableHeap::Create(BufferPoolManager* bpm) {
  page_id_t pid;
  Page* page = bpm->NewPage(&pid);
  if (!page) throw DBError("TableHeap::Create: buffer pool full");
  TablePage tp(page);
  tp.Init();
  bpm->UnpinPage(pid, true);
  return pid;
}

RID TableHeap::InsertTuple(const std::string& data) {
  page_id_t pid = first_page_id_;
  while (true) {
    Page* page = bpm_->FetchPage(pid);
    if (!page) throw DBError("TableHeap::InsertTuple: buffer pool full");
    TablePage tp(page);
    int slot = tp.InsertTuple(data);
    if (slot >= 0) {
      bpm_->UnpinPage(pid, true);
      return RID{pid, slot};
    }
    // Page is full -- move to next, or append a new page to the chain.
    page_id_t next = tp.GetNextPageId();
    if (next == INVALID_PAGE_ID) {
      page_id_t new_pid;
      Page* new_page = bpm_->NewPage(&new_pid);
      if (!new_page) { bpm_->UnpinPage(pid, false); throw DBError("buffer pool full"); }
      TablePage ntp(new_page);
      ntp.Init();
      int s = ntp.InsertTuple(data);
      tp.SetNextPageId(new_pid);
      bpm_->UnpinPage(pid, true);
      bpm_->UnpinPage(new_pid, true);
      return RID{new_pid, s};
    }
    bpm_->UnpinPage(pid, false);
    pid = next;
  }
}

bool TableHeap::GetTuple(const RID& rid, std::string* out) {
  Page* page = bpm_->FetchPage(rid.page_id);
  if (!page) return false;
  TablePage tp(page);
  bool ok = tp.GetTuple(rid.slot_id, out);
  bpm_->UnpinPage(rid.page_id, false);
  return ok;
}

bool TableHeap::DeleteTuple(const RID& rid) {
  Page* page = bpm_->FetchPage(rid.page_id);
  if (!page) return false;
  TablePage tp(page);
  bool ok = tp.DeleteTuple(rid.slot_id);
  bpm_->UnpinPage(rid.page_id, ok);
  return ok;
}

void TableHeap::Iterator::AdvanceToLive() {
  while (rid_.page_id != INVALID_PAGE_ID) {
    Page* page = heap_->bpm_->FetchPage(rid_.page_id);
    if (!page) { rid_.page_id = INVALID_PAGE_ID; return; }
    TablePage tp(page);
    int n = tp.GetNumSlots();
    while (rid_.slot_id < n) {
      if (tp.GetTuple(rid_.slot_id, &data_)) {
        heap_->bpm_->UnpinPage(rid_.page_id, false);
        return;  // positioned on a live tuple
      }
      rid_.slot_id++;
    }
    // Exhausted this page: advance to the next in the chain.
    page_id_t next = tp.GetNextPageId();
    heap_->bpm_->UnpinPage(rid_.page_id, false);
    rid_.page_id = next;
    rid_.slot_id = 0;
  }
}

}  // namespace minidb
