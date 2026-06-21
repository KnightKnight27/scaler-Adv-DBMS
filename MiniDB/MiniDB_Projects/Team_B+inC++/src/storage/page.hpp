#pragma once

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>

#include "../common/config.hpp"
#include "../common/types.hpp"

// ── Slotted page layout ────────────────────────────────────────────────────
//
//   +---------------------------------------------------------------+
//   | PageHeader | Slot 0 | Slot 1 | ... ->        free      <- tupleN .. tuple0 |
//   +---------------------------------------------------------------+
//   0           4                                                   PAGE_SIZE
//
// The slot directory grows left-to-right from just after the header; tuple
// bytes grow right-to-left from the end of the page. Free space is the gap in
// the middle. A slot stores a tuple's (offset, length); length == 0 marks a
// deleted slot. Indirection through slots means a tuple's RowID (page, slot)
// stays stable even though the raw bytes live wherever there was room.
//
// SlottedPage does NOT own memory — it is a thin view over a 4 KB buffer that
// the buffer pool owns (the frame). All it does is interpret those bytes.

struct PageHeader {
    std::uint16_t num_slots;       // entries in the slot directory
    std::uint16_t free_space_end;  // tuple data occupies [free_space_end, PAGE_SIZE)
};

struct Slot {
    std::uint16_t offset;  // where this tuple's bytes start in the page
    std::uint16_t length;  // tuple length in bytes; 0 == deleted
};

class SlottedPage {
public:
    explicit SlottedPage(char* data) : data_(data) {}

    // Stamp a brand-new (zeroed) page with an empty directory.
    void init() {
        header()->num_slots = 0;
        header()->free_space_end = static_cast<std::uint16_t>(PAGE_SIZE);
    }

    // Copy `tuple` into the page and return its new slot id, or nullopt if the
    // page is full. We never reuse deleted slots here (kept minimal).
    std::optional<SlotId> insert(const std::string& tuple) {
        int len = static_cast<int>(tuple.size());
        int need = len + static_cast<int>(sizeof(Slot));  // tuple bytes + one directory entry
        if (need > free_space()) return std::nullopt;

        std::uint16_t new_off = static_cast<std::uint16_t>(header()->free_space_end - len);
        std::memcpy(data_ + new_off, tuple.data(), static_cast<std::size_t>(len));
        header()->free_space_end = new_off;

        SlotId idx = header()->num_slots;
        slot(idx)->offset = new_off;
        slot(idx)->length = static_cast<std::uint16_t>(len);
        header()->num_slots++;
        return idx;
    }

    // Read the tuple at `idx`; nullopt if out of range or deleted.
    std::optional<std::string> get(SlotId idx) const {
        if (idx >= header()->num_slots) return std::nullopt;
        const Slot* s = slot(idx);
        if (s->length == 0) return std::nullopt;
        return std::string(data_ + s->offset, s->length);
    }

    // Logically delete a tuple (length = 0). Its bytes are not reclaimed.
    bool erase(SlotId idx) {
        if (idx >= header()->num_slots) return false;
        if (slot(idx)->length == 0) return false;
        slot(idx)->length = 0;
        return true;
    }

    std::uint16_t slot_count() const { return header()->num_slots; }

private:
    char* data_;

    // Reinterpret the raw page bytes as our header / slot-directory structs.
    // Offsets are 4-byte multiples and the frame buffer is 4-byte aligned, so
    // these accesses are aligned. This is the standard on-disk-struct idiom.
    PageHeader* header() { return reinterpret_cast<PageHeader*>(data_); }
    const PageHeader* header() const { return reinterpret_cast<const PageHeader*>(data_); }

    Slot* slot(SlotId i) {
        return reinterpret_cast<Slot*>(data_ + sizeof(PageHeader)) + i;
    }
    const Slot* slot(SlotId i) const {
        return reinterpret_cast<const Slot*>(data_ + sizeof(PageHeader)) + i;
    }

    // Bytes between the end of the slot directory and the start of tuple data.
    int free_space() const {
        int dir_end = static_cast<int>(sizeof(PageHeader)) +
                      header()->num_slots * static_cast<int>(sizeof(Slot));
        return static_cast<int>(header()->free_space_end) - dir_end;
    }
};
