#include "storage/table_heap.h"
#include <cstring>

namespace minidb {

namespace {
TablePageHeader *header_of(Page *p) {
    return reinterpret_cast<TablePageHeader *>(p->data());
}
Slot *slots_of(Page *p) {
    return reinterpret_cast<Slot *>(p->data() + sizeof(TablePageHeader));
}
// Initialise a freshly allocated heap page.
void init_page(Page *p) {
    TablePageHeader *h = header_of(p);
    h->next_page_id = INVALID_PAGE_ID;
    h->num_slots = 0;
    h->free_offset = static_cast<int32_t>(PAGE_SIZE);
}
// Bytes available for a new (record + slot) on this page.
int free_space(Page *p) {
    TablePageHeader *h = header_of(p);
    int dir_end = static_cast<int>(sizeof(TablePageHeader)) + h->num_slots * (int)sizeof(Slot);
    return h->free_offset - dir_end;
}
} // namespace

bool TableHeap::insert_on_page(page_id_t page_id, const std::vector<char> &record, RID *out_rid) {
    Page *pg = bpm_->fetch_page(page_id);
    TablePageHeader *h = header_of(pg);
    int need = static_cast<int>(record.size()) + (int)sizeof(Slot);
    if (free_space(pg) < need) {
        bpm_->unpin_page(page_id, false);
        return false;
    }
    // Write record bytes at the back, growing downward.
    int32_t rec_off = h->free_offset - static_cast<int32_t>(record.size());
    std::memcpy(pg->data() + rec_off, record.data(), record.size());
    h->free_offset = rec_off;
    // Append a slot-directory entry at the front.
    Slot *slots = slots_of(pg);
    int32_t slot_id = h->num_slots;
    slots[slot_id].offset = rec_off;
    slots[slot_id].length = static_cast<int32_t>(record.size());
    h->num_slots++;
    bpm_->unpin_page(page_id, true);
    *out_rid = RID{page_id, slot_id};
    return true;
}

RID TableHeap::insert_tuple(const Tuple &tuple) {
    std::vector<char> record = SerializeTuple(tuple, *schema_);

    // Empty table: allocate the first page.
    if (first_page_id_ == INVALID_PAGE_ID) {
        page_id_t pid;
        Page *pg = bpm_->new_page(&pid);
        init_page(pg);
        bpm_->unpin_page(pid, true);
        first_page_id_ = pid;
        last_page_id_  = pid;
    }

    // If the heap was opened on an existing chain, find the tail once. After
    // this, inserts append to last_page_id_ in O(1) (we never reclaim space in
    // the middle, so the tail is always the right place to grow).
    if (last_page_id_ == INVALID_PAGE_ID) {
        page_id_t cur = first_page_id_;
        while (true) {
            Page *pg = bpm_->fetch_page(cur);
            page_id_t nxt = header_of(pg)->next_page_id;
            bpm_->unpin_page(cur, false);
            if (nxt == INVALID_PAGE_ID) break;
            cur = nxt;
        }
        last_page_id_ = cur;
    }

    // Fast path: append to the tail page.
    RID rid;
    if (insert_on_page(last_page_id_, record, &rid)) return rid;

    // Tail is full: allocate a new page, link it after the current tail, and
    // advance the tail pointer.
    page_id_t pid;
    Page *npg = bpm_->new_page(&pid);
    init_page(npg);
    bpm_->unpin_page(pid, true);

    Page *lpg = bpm_->fetch_page(last_page_id_);
    header_of(lpg)->next_page_id = pid;
    bpm_->unpin_page(last_page_id_, true);
    last_page_id_ = pid;

    bool ok = insert_on_page(pid, record, &rid);
    (void)ok; // a fresh page always has room for one record
    return rid;
}

bool TableHeap::get_tuple(const RID &rid, Tuple *out) {
    if (rid.page_id == INVALID_PAGE_ID) return false;
    Page *pg = bpm_->fetch_page(rid.page_id);
    TablePageHeader *h = header_of(pg);
    if (rid.slot_id < 0 || rid.slot_id >= h->num_slots) {
        bpm_->unpin_page(rid.page_id, false);
        return false;
    }
    Slot s = slots_of(pg)[rid.slot_id];
    if (s.length < 0) { // tombstone
        bpm_->unpin_page(rid.page_id, false);
        return false;
    }
    if (out) *out = DeserializeTuple(pg->data() + s.offset, *schema_);
    bpm_->unpin_page(rid.page_id, false);
    return true;
}

bool TableHeap::delete_tuple(const RID &rid) {
    if (rid.page_id == INVALID_PAGE_ID) return false;
    Page *pg = bpm_->fetch_page(rid.page_id);
    TablePageHeader *h = header_of(pg);
    if (rid.slot_id < 0 || rid.slot_id >= h->num_slots) {
        bpm_->unpin_page(rid.page_id, false);
        return false;
    }
    Slot *slots = slots_of(pg);
    if (slots[rid.slot_id].length < 0) {
        bpm_->unpin_page(rid.page_id, false);
        return false;
    }
    slots[rid.slot_id].length = -1; // tombstone (space not reclaimed)
    bpm_->unpin_page(rid.page_id, true);
    return true;
}

std::vector<std::pair<RID, Tuple>> TableHeap::scan() {
    std::vector<std::pair<RID, Tuple>> out;
    page_id_t cur = first_page_id_;
    while (cur != INVALID_PAGE_ID) {
        Page *pg = bpm_->fetch_page(cur);
        TablePageHeader *h = header_of(pg);
        Slot *slots = slots_of(pg);
        for (int i = 0; i < h->num_slots; ++i) {
            if (slots[i].length < 0) continue; // skip tombstones
            Tuple t = DeserializeTuple(pg->data() + slots[i].offset, *schema_);
            out.emplace_back(RID{cur, i}, std::move(t));
        }
        page_id_t nxt = h->next_page_id;
        bpm_->unpin_page(cur, false);
        cur = nxt;
    }
    return out;
}

} // namespace minidb
