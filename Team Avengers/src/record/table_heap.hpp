// ============================================================================
//  table_heap.hpp — A table's rows stored as a linked list of SLOTTED PAGES.
//
//  Heap file = unordered collection of tuples spread across pages, chained by a
//  next-page pointer. Each page is a SLOTTED PAGE, the standard layout for
//  variable-length records:
//
//     +----------------------------------------------------------+
//     | header | slot[0] slot[1] ... ->        <- ... tuple bytes |
//     +----------------------------------------------------------+
//                 (slot dir grows right)       (tuples grow left)
//
//  The slot directory grows forward from the header; tuple bytes grow backward
//  from the end of the page. Free space is the gap between them. A slot stores
//  (offset, length); length 0 is a tombstone (deleted row). Tombstoning instead
//  of compacting keeps a RID stable forever — the B+ tree index can point at a
//  slot and trust it won't move.
// ============================================================================
#pragma once

#include "../storage/buffer_pool.hpp"
#include "tuple.hpp"

#include <cstring>
#include <functional>

namespace minidb {

// --- page header layout (byte offsets within a page) ------------------------
namespace tp {
    constexpr int OFF_NEXT      = 0;   // int32  next page id in the chain
    constexpr int OFF_NUM_SLOTS = 4;   // uint16 number of slots (incl. dead)
    constexpr int OFF_FREE_PTR  = 6;   // uint16 start of tuple-data region
    constexpr int HEADER_SIZE   = 8;
    constexpr int SLOT_SIZE     = 4;   // uint16 offset + uint16 length
}

// Thin typed view over a raw Page's bytes. Holds no state of its own.
class SlottedPage {
public:
    explicit SlottedPage(Page* p) : p_(p) {}

    void init() {
        set_i32(tp::OFF_NEXT, INVALID_PAGE_ID);
        set_u16(tp::OFF_NUM_SLOTS, 0);
        set_u16(tp::OFF_FREE_PTR, PAGE_SIZE);   // tuples start at the very end
    }

    page_id_t next() const      { return get_i32(tp::OFF_NEXT); }
    void set_next(page_id_t id)  { set_i32(tp::OFF_NEXT, id); }
    uint16_t num_slots() const   { return get_u16(tp::OFF_NUM_SLOTS); }

    // Try to place `rec` on this page. On success returns the slot number;
    // returns -1 if the page lacks room (caller then allocates a new page).
    int insert(const std::string& rec) {
        uint16_t free_ptr = get_u16(tp::OFF_FREE_PTR);
        uint16_t slots    = get_u16(tp::OFF_NUM_SLOTS);
        size_t slot_dir_end = tp::HEADER_SIZE + (size_t)(slots + 1) * tp::SLOT_SIZE;
        // need room for the record AND one more slot entry
        if (free_ptr < rec.size() || free_ptr - rec.size() < slot_dir_end) return -1;
        uint16_t new_off = (uint16_t)(free_ptr - rec.size());
        std::memcpy(p_->data + new_off, rec.data(), rec.size());
        set_slot(slots, new_off, (uint16_t)rec.size());
        set_u16(tp::OFF_FREE_PTR, new_off);
        set_u16(tp::OFF_NUM_SLOTS, slots + 1);
        return slots;
    }

    // Read slot `s`. Returns false if the slot is a tombstone (deleted).
    bool get(int s, std::string* out) const {
        uint16_t off, len;
        slot(s, &off, &len);
        if (len == 0) return false;          // deleted
        out->assign(p_->data + off, len);
        return true;
    }

    // Tombstone slot `s`. The bytes stay (we don't compact) but length->0.
    bool erase(int s) {
        uint16_t off, len;
        slot(s, &off, &len);
        if (len == 0) return false;
        set_slot(s, off, 0);
        return true;
    }

private:
    // little-endian fixed-width accessors over the page buffer
    int32_t  get_i32(int o) const { int32_t v;  std::memcpy(&v, p_->data + o, 4); return v; }
    uint16_t get_u16(int o) const { uint16_t v; std::memcpy(&v, p_->data + o, 2); return v; }
    void set_i32(int o, int32_t v)  { std::memcpy(p_->data + o, &v, 4); }
    void set_u16(int o, uint16_t v) { std::memcpy(p_->data + o, &v, 2); }

    void slot(int s, uint16_t* off, uint16_t* len) const {
        int base = tp::HEADER_SIZE + s * tp::SLOT_SIZE;
        *off = get_u16(base);
        *len = get_u16(base + 2);
    }
    void set_slot(int s, uint16_t off, uint16_t len) {
        int base = tp::HEADER_SIZE + s * tp::SLOT_SIZE;
        set_u16(base, off);
        set_u16(base + 2, len);
    }

    Page* p_;
};

// ---------------------------------------------------------------------------
//  TableHeap — owns the page chain for one table.
// ---------------------------------------------------------------------------
class TableHeap {
public:
    TableHeap(BufferPoolManager* bpm, page_id_t first_page)
        : bpm_(bpm), first_page_(first_page) {}

    // Create a fresh, empty heap (one initialised page). Returns its first pid.
    static page_id_t create(BufferPoolManager* bpm) {
        page_id_t pid;
        Page* p = bpm->new_page(&pid);
        SlottedPage(p).init();
        bpm->unpin_page(pid, /*dirty=*/true);
        return pid;
    }

    page_id_t first_page() const { return first_page_; }

    // Insert a serialized record somewhere in the chain; returns its RID.
    // Walks to the last page, tries to insert, else links a new page.
    RID insert(const std::string& rec) {
        page_id_t pid = first_page_;
        while (true) {
            Page* p = bpm_->fetch_page(pid);
            SlottedPage sp(p);
            int slot = sp.insert(rec);
            if (slot >= 0) {
                bpm_->unpin_page(pid, true);
                return RID{pid, (uint16_t)slot};
            }
            page_id_t nxt = sp.next();
            if (nxt == INVALID_PAGE_ID) {            // end of chain: grow it
                page_id_t newpid;
                Page* np = bpm_->new_page(&newpid);
                SlottedPage nsp(np);
                nsp.init();
                int s = nsp.insert(rec);
                bpm_->unpin_page(newpid, true);
                sp.set_next(newpid);
                bpm_->unpin_page(pid, true);
                return RID{newpid, (uint16_t)s};
            }
            bpm_->unpin_page(pid, false);
            pid = nxt;
        }
    }

    bool get(RID rid, std::string* out) {
        Page* p = bpm_->fetch_page(rid.page_id);
        bool ok = SlottedPage(p).get(rid.slot_num, out);
        bpm_->unpin_page(rid.page_id, false);
        return ok;
    }

    bool erase(RID rid) {
        Page* p = bpm_->fetch_page(rid.page_id);
        bool ok = SlottedPage(p).erase(rid.slot_num);
        bpm_->unpin_page(rid.page_id, ok);
        return ok;
    }

    // Full table scan: invoke `fn(rid, bytes)` for every live tuple. This is
    // the sequential-scan access path the executor uses when no index applies.
    void scan(const std::function<void(RID, const std::string&)>& fn) {
        page_id_t pid = first_page_;
        while (pid != INVALID_PAGE_ID) {
            Page* p = bpm_->fetch_page(pid);
            SlottedPage sp(p);
            uint16_t n = sp.num_slots();
            for (uint16_t s = 0; s < n; ++s) {
                std::string rec;
                if (sp.get(s, &rec)) fn(RID{pid, s}, rec);
            }
            page_id_t nxt = sp.next();
            bpm_->unpin_page(pid, false);
            pid = nxt;
        }
    }

private:
    BufferPoolManager* bpm_;
    page_id_t first_page_;
};

}  // namespace minidb
