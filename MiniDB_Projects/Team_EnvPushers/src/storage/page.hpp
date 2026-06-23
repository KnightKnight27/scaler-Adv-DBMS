// Slotted-page layout for fixed-size heap-file pages.
//
//   +----------------------------------------------------------------+
//   | header | slot0 slot1 ... |   free space   | ... recN rec1 rec0 |
//   +----------------------------------------------------------------+
//            slot dir grows -> |                | <- records grow
//
// Records are appended from the end of the page toward the middle; the slot
// directory grows from just past the header toward the middle.  A record is
// addressed by a slot id; a slot with length 0 is a tombstone (deleted), which
// keeps RIDs stable.  This is the unit moved between memory and disk.
#pragma once

#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

#include "common/types.hpp"

namespace minidb {

constexpr uint32_t PAGE_SIZE = 4096;

class Page {
public:
    // On-disk header at offset 0 of every page.
    struct Header {
        PageId  page_id;
        PageId  next_page_id;   // chain link for heap files (-1 if none)
        uint16_t num_slots;
        uint16_t free_ptr;      // offset where the free region ends (record area start)
        uint8_t  page_type;     // reserved; 0 = heap
    };

    struct Slot {
        uint16_t offset;
        uint16_t length;        // 0 => deleted tombstone
    };

    static constexpr uint32_t HEADER_SIZE = sizeof(Header);
    static constexpr uint32_t SLOT_SIZE   = sizeof(Slot);

    Page() { data_.resize(PAGE_SIZE, 0); }

    // Build a page view that owns its buffer; init==true lays down a fresh header.
    explicit Page(PageId pid, bool init = true) {
        data_.resize(PAGE_SIZE, 0);
        if (init) {
            Header h{};
            h.page_id = pid;
            h.next_page_id = INVALID_PAGE_ID;
            h.num_slots = 0;
            h.free_ptr = PAGE_SIZE;
            h.page_type = 0;
            std::memcpy(data_.data(), &h, sizeof(h));
        }
    }

    uint8_t* raw() { return data_.data(); }
    const uint8_t* raw() const { return data_.data(); }
    std::vector<uint8_t>& buffer() { return data_; }

    Header header() const {
        Header h{};
        std::memcpy(&h, data_.data(), sizeof(h));
        return h;
    }
    void set_header(const Header& h) { std::memcpy(data_.data(), &h, sizeof(h)); }

    PageId page_id()      const { return header().page_id; }
    PageId next_page_id() const { return header().next_page_id; }
    void set_next_page_id(PageId pid) {
        Header h = header(); h.next_page_id = pid; set_header(h);
    }
    uint16_t num_slots()  const { return header().num_slots; }

    uint32_t free_space() const {
        Header h = header();
        uint32_t slot_end = HEADER_SIZE + h.num_slots * SLOT_SIZE;
        return (h.free_ptr > slot_end) ? (h.free_ptr - slot_end) : 0;
    }

    bool can_insert(uint32_t record_len) const {
        return free_space() >= record_len + SLOT_SIZE;
    }

    // Insert record bytes; returns slot id or nullopt if it does not fit.
    std::optional<SlotId> insert(const uint8_t* rec, uint16_t len) {
        if (!can_insert(len)) return std::nullopt;
        Header h = header();
        uint16_t new_off = h.free_ptr - len;
        std::memcpy(data_.data() + new_off, rec, len);
        Slot s{new_off, len};
        SlotId sid = h.num_slots;
        std::memcpy(data_.data() + slot_pos(sid), &s, sizeof(s));
        h.num_slots += 1;
        h.free_ptr = new_off;
        set_header(h);
        return sid;
    }

    std::optional<std::vector<uint8_t>> get(SlotId sid) const {
        if (sid >= num_slots()) return std::nullopt;
        Slot s = slot(sid);
        if (s.length == 0) return std::nullopt;  // tombstone
        return std::vector<uint8_t>(data_.begin() + s.offset,
                                    data_.begin() + s.offset + s.length);
    }

    bool erase(SlotId sid) {
        if (sid >= num_slots()) return false;
        Slot s = slot(sid);
        if (s.length == 0) return false;
        s.length = 0;  // tombstone; reclaimed on compaction
        write_slot(sid, s);
        return true;
    }

    // In-place update if the new record fits the old footprint, else false.
    bool update_inplace(SlotId sid, const uint8_t* rec, uint16_t len) {
        if (sid >= num_slots()) return false;
        Slot s = slot(sid);
        if (s.length == 0 || len > s.length) return false;
        std::memcpy(data_.data() + s.offset, rec, len);
        s.length = len;
        write_slot(sid, s);
        return true;
    }

private:
    uint32_t slot_pos(SlotId sid) const { return HEADER_SIZE + sid * SLOT_SIZE; }
    Slot slot(SlotId sid) const {
        Slot s{}; std::memcpy(&s, data_.data() + slot_pos(sid), sizeof(s)); return s;
    }
    void write_slot(SlotId sid, const Slot& s) {
        std::memcpy(data_.data() + slot_pos(sid), &s, sizeof(s));
    }

    std::vector<uint8_t> data_;
};

}  // namespace minidb
