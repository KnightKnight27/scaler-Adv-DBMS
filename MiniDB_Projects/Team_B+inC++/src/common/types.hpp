#pragma once

#include <cstdint>

// A page's position in the data file: file offset = page_id * PAGE_SIZE.
// Signed so we can use -1 as the "no such page" sentinel.
using PageID = std::int32_t;

// A tuple's position inside a page = its index in that page's slot directory.
using SlotId = std::uint16_t;

constexpr PageID INVALID_PAGE_ID = -1;

// A RowID uniquely identifies one tuple in the whole heap: which page, and
// which slot within that page. This is what the B+ tree will point at later.
struct RowID {
    PageID page_id = INVALID_PAGE_ID;
    SlotId slot = 0;

    bool operator==(const RowID& other) const {
        return page_id == other.page_id && slot == other.slot;
    }
};
