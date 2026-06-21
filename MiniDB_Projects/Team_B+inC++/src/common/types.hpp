#pragma once

#include <cstdint>

// file offset = page_id * PAGE_SIZE; -1 = no page
using PageID = std::int32_t;

using SlotId = std::uint16_t;  // index into page slot directory

constexpr PageID INVALID_PAGE_ID = -1;

// (page, slot) -> one tuple in the heap
struct RowID {
    PageID page_id = INVALID_PAGE_ID;
    SlotId slot = 0;

    bool operator==(const RowID& other) const {
        return page_id == other.page_id && slot == other.slot;
    }
};
