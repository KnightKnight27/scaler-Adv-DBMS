#pragma once
#include <cstdint>
#include <cstring>
#include "../common/config.hpp"
#include "../common/types.hpp"

namespace minidb {

// A view over a PAGE_SIZE byte buffer, interpreted as a *slotted page*.
//
//   +-----------------------------------------------------------------+
//   | header | slot 0 | slot 1 | ... ->        free        <- tuples   |
//   +-----------------------------------------------------------------+
//
// The slot directory grows left-to-right from just after the header; tuple
// bytes grow right-to-left from the end of the page. Free space is the gap
// between them. Each slot holds (offset, length); length == 0 marks a tuple
// that has been deleted. A RowId = (page, slot) therefore stays stable even if
// the tuple's bytes are never moved (we never compact, so they never move).
class SlottedPage {
public:
    explicit SlottedPage(char* data) : data_(data) {}

    // Header lives at the very start of the page.
    struct Header {
        uint16_t num_slots;     // number of slot-directory entries
        uint16_t free_ptr;      // offset where the tuple region begins (grows down)
    };

    Header* header() { return reinterpret_cast<Header*>(data_); }
    const Header* header() const { return reinterpret_cast<const Header*>(data_); }

    static constexpr uint16_t HEADER_SIZE = sizeof(Header);
    static constexpr uint16_t SLOT_SIZE = 4; // (uint16 offset, uint16 length)

    // Prepare a brand-new empty page.
    void init() {
        header()->num_slots = 0;
        header()->free_ptr = static_cast<uint16_t>(PAGE_SIZE);
    }

    uint16_t numSlots() const { return header()->num_slots; }

    // A page read back as all-zeros (e.g. allocated on disk but never flushed
    // before a crash) has free_ptr == 0, which is impossible for a real page
    // (tuples never reach offset 0). The heap file uses this to re-init such a
    // page before reusing it, instead of trusting the zeroed header.
    bool initialized() const { return header()->free_ptr != 0; }

    // Bytes available for one more (tuple + its slot). Saturating: never
    // underflows on a corrupt/uninitialized header.
    uint16_t freeSpace() const {
        uint16_t slot_dir_end = HEADER_SIZE + header()->num_slots * SLOT_SIZE;
        if (header()->free_ptr < slot_dir_end) return 0;
        return header()->free_ptr - slot_dir_end;
    }

    // Insert raw tuple bytes; returns the slot index, or -1 if it does not fit.
    int insertTuple(const char* src, uint16_t len) {
        if (freeSpace() < len + SLOT_SIZE) return -1;
        if (header()->free_ptr < len) return -1; // defensive: never write OOB
        uint16_t new_off = header()->free_ptr - len;
        std::memcpy(data_ + new_off, src, len);
        header()->free_ptr = new_off;
        int slot = header()->num_slots;
        setSlot(slot, new_off, len);
        header()->num_slots++;
        return slot;
    }

    // Fetch tuple bytes for a slot. Returns false for an out-of-range or
    // deleted (len == 0) slot.
    bool getTuple(int slot, const char*& out, uint16_t& len) const {
        if (slot < 0 || slot >= header()->num_slots) return false;
        uint16_t off, l;
        readSlot(slot, off, l);
        if (l == 0) return false;
        out = data_ + off;
        len = l;
        return true;
    }

    // Tombstone a slot (length := 0). The bytes are left in place.
    void eraseTuple(int slot) {
        if (slot < 0 || slot >= header()->num_slots) return;
        uint16_t off, l;
        readSlot(slot, off, l);
        setSlot(slot, off, 0);
    }

private:
    char* data_;

    char* slotPtr(int slot) { return data_ + HEADER_SIZE + slot * SLOT_SIZE; }
    const char* slotPtr(int slot) const { return data_ + HEADER_SIZE + slot * SLOT_SIZE; }

    void setSlot(int slot, uint16_t off, uint16_t len) {
        char* p = slotPtr(slot);
        std::memcpy(p, &off, 2);
        std::memcpy(p + 2, &len, 2);
    }
    void readSlot(int slot, uint16_t& off, uint16_t& len) const {
        const char* p = slotPtr(slot);
        std::memcpy(&off, p, 2);
        std::memcpy(&len, p + 2, 2);
    }
};

} // namespace minidb
