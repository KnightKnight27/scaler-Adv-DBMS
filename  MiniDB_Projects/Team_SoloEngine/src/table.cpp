#include "table.h"

#include <stdexcept>

static HeapPage *AsHeap(Page *p) { return reinterpret_cast<HeapPage *>(p->data); }

TableHeap::TableHeap(BufferPoolManager *bpm) : bpm_(bpm) {
    page_id_t pid;
    Page *p = bpm_->NewPage(pid);
    if (!p) throw std::runtime_error("TableHeap: cannot allocate first page");
    HeapPage *hp        = AsHeap(p);
    hp->num_tuples      = 0;
    hp->next_page_id    = INVALID_PAGE_ID;
    bpm_->UnpinPage(pid, true);
    first_page_id_ = last_page_id_ = pid;
}

RID TableHeap::InsertTuple(const Tuple &t) {
    Page *page = bpm_->FetchPage(last_page_id_);
    if (!page) throw std::runtime_error("TableHeap: cannot fetch last page");
    HeapPage *hp = AsHeap(page);

    if (hp->num_tuples >= HEAP_PAGE_TUPLES) {
        // Current last page is full — chain a new one.
        page_id_t new_pid;
        Page *new_page = bpm_->NewPage(new_pid);
        if (!new_page) {
            bpm_->UnpinPage(last_page_id_, false);
            throw std::runtime_error("TableHeap: pool full during insert");
        }
        HeapPage *new_hp    = AsHeap(new_page);
        new_hp->num_tuples  = 0;
        new_hp->next_page_id = INVALID_PAGE_ID;

        hp->next_page_id = new_pid;              // link old → new
        bpm_->UnpinPage(last_page_id_, true);    // old page flushed (dirty: next ptr updated)

        last_page_id_ = new_pid;
        page = new_page;
        hp   = new_hp;
    }

    int32_t slot     = hp->num_tuples;
    hp->tuples[slot] = t;
    hp->num_tuples++;
    num_tuples_++;

    RID rid{last_page_id_, slot};
    bpm_->UnpinPage(last_page_id_, true);
    return rid;
}

void TableHeap::DeleteTuple(RID rid) {
    if (!rid.IsValid()) return;
    Page *page = bpm_->FetchPage(rid.page_id);
    if (!page) return;
    HeapPage *hp = AsHeap(page);
    if (rid.slot_num >= hp->num_tuples) {
        bpm_->UnpinPage(rid.page_id, false);
        return;
    }
    hp->tuples[rid.slot_num] = {DELETED_TUPLE_ID, 0, 0};
    bpm_->UnpinPage(rid.page_id, true);
    if (num_tuples_ > 0) --num_tuples_;
}

bool TableHeap::GetTuple(RID rid, Tuple *out) const {
    if (!rid.IsValid()) return false;
    Page *page = bpm_->FetchPage(rid.page_id);
    if (!page) return false;
    HeapPage *hp = AsHeap(page);
    bool ok = rid.slot_num < hp->num_tuples;
    if (ok && out) *out = hp->tuples[rid.slot_num];
    bpm_->UnpinPage(rid.page_id, false);
    return ok;
}
