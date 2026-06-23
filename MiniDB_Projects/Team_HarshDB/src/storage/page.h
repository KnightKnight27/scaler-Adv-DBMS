#pragma once
// ---------------------------------------------------------------------------
// page.h - the slotted-page layout.
//
// This is the exact structure we studied in the SQLite/PostgreSQL labs: a
// header at the top, a slot directory growing downward, and tuple bytes packed
// upward from the bottom. The gap in the middle is free space. When it runs
// out the page is "full" and the heap file moves to the next page.
//
//   0          12                                                     4096
//   +----------+-----------+--------------------------+----------------+
//   | header   | slot dir  |       free space         |  tuple bytes   |
//   | (12 B)   | 8 B each  |   <--            -->     |  (grows left)  |
//   +----------+-----------+--------------------------+----------------+
//
// A slot stores (offset, length). length == -1 marks a deleted slot, but the
// slot number is never reused so an RID (page_id, slot) stays stable forever.
// ---------------------------------------------------------------------------
#include "../common.h"
#include <cstring>

namespace minidb {

class Page {
public:
    static constexpr int HEADER_SIZE = 12; // next_page_id, num_slots, free_ptr
    static constexpr int SLOT_SIZE   = 8;  // offset, length

    char data[PAGE_SIZE];

    void init() {
        std::memset(data, 0, PAGE_SIZE);
        set_next_page(INVALID_PAGE_ID);
        set_num_slots(0);
        set_free_ptr(PAGE_SIZE);
    }

    // ---- header accessors ----
    int  next_page() const          { return read_int(0); }
    void set_next_page(int v)        { write_int(0, v); }
    int  num_slots() const           { return read_int(4); }
    void set_num_slots(int v)        { write_int(4, v); }
    int  free_ptr() const            { return read_int(8); }
    void set_free_ptr(int v)         { write_int(8, v); }

    int free_space() const {
        return free_ptr() - (HEADER_SIZE + num_slots() * SLOT_SIZE);
    }

    // Insert raw bytes; returns the slot number, or -1 if the page is full.
    int insert(const char* bytes, int len) {
        if (len + SLOT_SIZE > free_space()) return -1;
        int slot = num_slots();
        int new_free = free_ptr() - len;
        std::memcpy(data + new_free, bytes, len);
        set_slot(slot, new_free, len);
        set_free_ptr(new_free);
        set_num_slots(slot + 1);
        return slot;
    }

    // Fetch bytes for a slot. Returns false if the slot is deleted/out of range.
    bool get(int slot, const char*& out_ptr, int& out_len) const {
        if (slot < 0 || slot >= num_slots()) return false;
        int off = slot_offset(slot);
        int len = slot_length(slot);
        if (len < 0) return false; // deleted
        out_ptr = data + off;
        out_len = len;
        return true;
    }

    // Tombstone a slot. The bytes stay but become unreachable.
    void remove(int slot) {
        if (slot >= 0 && slot < num_slots()) set_slot_length(slot, -1);
    }

    // Overwrite a slot's bytes in place (used by MVCC to stamp xmax).
    // Only safe when the new length equals the old length.
    bool overwrite(int slot, const char* bytes, int len) {
        if (slot < 0 || slot >= num_slots()) return false;
        if (slot_length(slot) != len) return false;
        std::memcpy(data + slot_offset(slot), bytes, len);
        return true;
    }

private:
    int read_int(int off) const {
        int v; std::memcpy(&v, data + off, sizeof(int)); return v;
    }
    void write_int(int off, int v) {
        std::memcpy(data + off, &v, sizeof(int));
    }
    int slot_pos(int slot) const   { return HEADER_SIZE + slot * SLOT_SIZE; }
    int slot_offset(int slot) const{ return read_int(slot_pos(slot)); }
    int slot_length(int slot) const{ return read_int(slot_pos(slot) + 4); }
    void set_slot(int slot, int off, int len) {
        write_int(slot_pos(slot), off);
        write_int(slot_pos(slot) + 4, len);
    }
    void set_slot_length(int slot, int len) {
        write_int(slot_pos(slot) + 4, len);
    }
};

} // namespace minidb
