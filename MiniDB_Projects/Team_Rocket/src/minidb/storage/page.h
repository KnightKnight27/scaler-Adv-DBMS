#pragma once

#include <cstdint>
#include <cstring>

#include "disk_manager.h"

namespace minidb {

// Slotted-page layout over a raw PAGE_SIZE buffer.
//
//   [ num_slots:int32 | free_end:int32 | lsn:int64 ] header (16 bytes)
//   [ slot0 | slot1 | ... ]                         slot directory grows up
//   ...... free space ......
//   [ ... tuple data ... ]                          data region grows down
//
// Each slot is (offset:int32, length:int32); offset == -1 marks a deleted slot.
class Page {
public:
    static constexpr int HEADER = 16;
    static constexpr int SLOT = 8;

    explicit Page(uint8_t* data) : data_(data) {}

    void init() {
        set_num_slots(0);
        set_free_end(PAGE_SIZE);
        set_lsn(0);
    }

    // A freshly read zero page has free_end == 0, which is never valid for a
    // live page; treat that as "needs initialising".
    void ensure_init() {
        if (free_end() == 0 && num_slots() == 0) init();
    }

    int32_t num_slots() const { return rd32(0); }
    int32_t free_end() const { return rd32(4); }
    int64_t lsn() const { return rd64(8); }
    void set_num_slots(int32_t v) { wr32(0, v); }
    void set_free_end(int32_t v) { wr32(4, v); }
    void set_lsn(int64_t v) { wr64(8, v); }

    int slot_offset(int s) const { return rd32(HEADER + s * SLOT); }
    int slot_len(int s) const { return rd32(HEADER + s * SLOT + 4); }
    void set_slot(int s, int off, int len) {
        wr32(HEADER + s * SLOT, off);
        wr32(HEADER + s * SLOT + 4, len);
    }

    int free_space() const { return free_end() - (HEADER + num_slots() * SLOT); }

    // Append a tuple, returning its slot id, or -1 if the page is full.
    int insert(const uint8_t* bytes, int len) {
        ensure_init();
        if (free_space() < len + SLOT) return -1;
        int off = free_end() - len;
        std::memcpy(data_ + off, bytes, len);
        int s = num_slots();
        set_num_slots(s + 1);
        set_free_end(off);
        set_slot(s, off, len);
        return s;
    }

    // Place a tuple at a specific slot, growing the directory if needed.
    // Used by recovery to replay a logged insert at its original RID.
    bool put_at(int s, const uint8_t* bytes, int len) {
        ensure_init();
        while (num_slots() <= s) {
            if (free_space() < SLOT) return false;
            int ns = num_slots();
            set_slot(ns, -1, 0);
            set_num_slots(ns + 1);
        }
        if (free_space() < len) return false;
        int off = free_end() - len;
        std::memcpy(data_ + off, bytes, len);
        set_free_end(off);
        set_slot(s, off, len);
        return true;
    }

    bool get(int s, const uint8_t*& out, int& len) const {
        if (s < 0 || s >= num_slots()) return false;
        int off = slot_offset(s);
        if (off < 0) return false;
        out = data_ + off;
        len = slot_len(s);
        return true;
    }

    void mark_delete(int s) {
        if (s >= 0 && s < num_slots()) set_slot(s, -1, 0);
    }

private:
    uint8_t* data_;

    int32_t rd32(int o) const {
        int32_t v;
        std::memcpy(&v, data_ + o, 4);
        return v;
    }
    int64_t rd64(int o) const {
        int64_t v;
        std::memcpy(&v, data_ + o, 8);
        return v;
    }
    void wr32(int o, int32_t v) { std::memcpy(data_ + o, &v, 4); }
    void wr64(int o, int64_t v) { std::memcpy(data_ + o, &v, 8); }
};

}  // namespace minidb
