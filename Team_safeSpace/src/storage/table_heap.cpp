#include "storage/table_heap.h"

namespace minidb {

TableHeap::TableHeap(BufferPoolManager *bpm, page_id_t first_page_id)
    : bpm_(bpm), first_page_id_(first_page_id), last_page_id_(first_page_id) {
  // Walk the page chain once to find the tail, so appends start there.
  page_id_t pid = first_page_id_;
  while (pid != INVALID_PAGE_ID) {
    Page *page = bpm_->FetchPage(pid);
    if (page == nullptr) break;
    page_id_t next = TablePage::GetNextPageId(page);
    bpm_->UnpinPage(pid, false);
    if (next == INVALID_PAGE_ID) {
      last_page_id_ = pid;
      break;
    }
    pid = next;
  }
}

page_id_t TableHeap::CreateNew(BufferPoolManager *bpm) {
  page_id_t pid;
  Page *page = bpm->NewPage(&pid);
  TablePage::Init(page);
  bpm->UnpinPage(pid, true);
  return pid;
}

bool TableHeap::InsertTuple(const Tuple &t, RID *rid) {
  if (t.Size() == 0 || t.Size() > TablePage::MaxTupleSize()) return false;

  page_id_t pid = last_page_id_;
  while (true) {
    Page *page = bpm_->FetchPage(pid);
    if (page == nullptr) return false;
    page->WLatch();

    slot_id_t slot;
    if (TablePage::InsertTuple(page, t, &slot)) {
      page->WUnlatch();
      bpm_->UnpinPage(pid, true);
      *rid = RID(pid, slot);
      last_page_id_ = pid;
      return true;
    }

    page_id_t next = TablePage::GetNextPageId(page);
    if (next == INVALID_PAGE_ID) {
      // Tail page is full: allocate and link a new page, then insert there.
      page_id_t new_pid;
      Page *np = bpm_->NewPage(&new_pid);
      if (np == nullptr) {
        page->WUnlatch();
        bpm_->UnpinPage(pid, false);
        return false;
      }
      TablePage::Init(np);
      slot_id_t s2;
      bool ok = TablePage::InsertTuple(np, t, &s2);
      TablePage::SetNextPageId(page, new_pid);
      page->WUnlatch();
      bpm_->UnpinPage(new_pid, true);
      bpm_->UnpinPage(pid, true);
      if (!ok) return false;
      *rid = RID(new_pid, s2);
      last_page_id_ = new_pid;
      return true;
    }

    page->WUnlatch();
    bpm_->UnpinPage(pid, false);
    pid = next;
  }
}

bool TableHeap::GetTuple(const RID &rid, Tuple *out) {
  Page *page = bpm_->FetchPage(rid.GetPageId());
  if (page == nullptr) return false;
  page->RLatch();
  bool ok = TablePage::GetTuple(page, rid.GetSlotNum(), out);
  page->RUnlatch();
  bpm_->UnpinPage(rid.GetPageId(), false);
  if (ok) out->SetRid(rid);
  return ok;
}

bool TableHeap::MarkDelete(const RID &rid) {
  Page *page = bpm_->FetchPage(rid.GetPageId());
  if (page == nullptr) return false;
  page->WLatch();
  bool ok = TablePage::MarkDelete(page, rid.GetSlotNum());
  page->WUnlatch();
  bpm_->UnpinPage(rid.GetPageId(), ok);
  return ok;
}

uint16_t TableHeap::PeekSlotOffset(const RID &rid) {
  Page *page = bpm_->FetchPage(rid.GetPageId());
  if (page == nullptr) return 0;
  page->RLatch();
  uint16_t off = TablePage::GetSlotOffset(page, rid.GetSlotNum());
  page->RUnlatch();
  bpm_->UnpinPage(rid.GetPageId(), false);
  return off;
}

bool TableHeap::RestoreSlot(const RID &rid, uint16_t offset) {
  Page *page = bpm_->FetchPage(rid.GetPageId());
  if (page == nullptr) return false;
  page->WLatch();
  TablePage::SetSlotOffset(page, rid.GetSlotNum(), offset);
  page->WUnlatch();
  bpm_->UnpinPage(rid.GetPageId(), true);
  return true;
}

// --- Iterator ---------------------------------------------------------------

TableHeap::Iterator::Iterator(TableHeap *heap, RID rid, bool end)
    : heap_(heap), rid_(rid), end_(end) {
  if (!end_) AdvanceToValid();
}

void TableHeap::Iterator::AdvanceToValid() {
  while (true) {
    page_id_t pid = rid_.GetPageId();
    if (pid == INVALID_PAGE_ID) {
      end_ = true;
      return;
    }
    Page *page = heap_->bpm_->FetchPage(pid);
    if (page == nullptr) {
      end_ = true;
      return;
    }
    uint16_t num = TablePage::GetNumSlots(page);
    slot_id_t slot = rid_.GetSlotNum();
    while (slot < num) {
      if (!TablePage::IsDeleted(page, slot)) {
        rid_ = RID(pid, slot);
        heap_->bpm_->UnpinPage(pid, false);
        return;
      }
      slot++;
    }
    page_id_t next = TablePage::GetNextPageId(page);
    heap_->bpm_->UnpinPage(pid, false);
    rid_ = RID(next, 0);  // continue at the head of the next page
  }
}

bool TableHeap::Iterator::operator!=(const Iterator &o) const {
  if (end_ || o.end_) return end_ != o.end_;
  return rid_ != o.rid_;
}

TableHeap::Iterator &TableHeap::Iterator::operator++() {
  rid_ = RID(rid_.GetPageId(), rid_.GetSlotNum() + 1);
  AdvanceToValid();
  return *this;
}

Tuple TableHeap::Iterator::operator*() {
  Tuple t;
  heap_->GetTuple(rid_, &t);
  return t;
}

TableHeap::Iterator TableHeap::Begin() { return Iterator(this, RID(first_page_id_, 0), false); }
TableHeap::Iterator TableHeap::End() { return Iterator(this, RID(INVALID_PAGE_ID, 0), true); }

}  // namespace minidb
