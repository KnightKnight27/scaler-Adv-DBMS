#pragma once

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>

#include "../common/config.hpp"
#include "../common/types.hpp"

// slotted page: header + slot directory grows ->, tuple bytes grow <- from page end; slot.length==0 = deleted

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

    // empty directory on a fresh page
    void init() {
        header()->num_slots = 0;
        header()->free_space_end = static_cast<std::uint16_t>(PAGE_SIZE);
    }

    // nullopt if full; doesn't reuse deleted slots
    std::optional<SlotId> insert(const std::string& tuple) {
        int len = static_cast<int>(tuple.size());
        int need = len + static_cast<int>(sizeof(Slot));  // tuple + 1 slot
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

    // nullopt if out of range or deleted
    std::optional<std::string> get(SlotId idx) const {
        if (idx >= header()->num_slots) return std::nullopt;
        const Slot* s = slot(idx);
        if (s->length == 0) return std::nullopt;
        return std::string(data_ + s->offset, s->length);
    }

    // length=0; bytes not reclaimed
    bool erase(SlotId idx) {
        if (idx >= header()->num_slots) return false;
        if (slot(idx)->length == 0) return false;
        slot(idx)->length = 0;
        return true;
    }

    std::uint16_t slot_count() const { return header()->num_slots; }

private:
    char* data_;

    PageHeader* header() { return reinterpret_cast<PageHeader*>(data_); }
    const PageHeader* header() const { return reinterpret_cast<const PageHeader*>(data_); }

    Slot* slot(SlotId i) {
        return reinterpret_cast<Slot*>(data_ + sizeof(PageHeader)) + i;
    }
    const Slot* slot(SlotId i) const {
        return reinterpret_cast<const Slot*>(data_ + sizeof(PageHeader)) + i;
    }

    // gap between directory end and tuple data
    int free_space() const {
        int dir_end = static_cast<int>(sizeof(PageHeader)) +
                      header()->num_slots * static_cast<int>(sizeof(Slot));
        return static_cast<int>(header()->free_space_end) - dir_end;
    }
};
