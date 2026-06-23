#pragma once

#include "common/types.h"
#include "common/config.h"
#include <cstdint>
#include <cstring>

/**
 * @struct PageHeader
 * @brief Fixed-size header stored at the very beginning of every database page.
 *
 * This header manages slot tracking and boundary offsets.
 * Layout overview of a slotted page:
 * +----------------------------------------------------------------+
 * | PageHeader (Fixed footprint)                                   |
 * +----------------------------------------------------------------+
 * | Slot[0] | Slot[1] | ... | Slot[N-1]  (Slot array grows forward) |
 * +--------------------------+-------------------------------------+
 * | <--- Free Space Area --->|                                     |
 * +--------------------------+                                     |
 * |                          | Record payloads (grow backward)     |
 * +----------------------------------------------------------------+
 */
struct PageHeader {
    PageID page_id;          ///< Unique ID of this page assigned upon allocation
    uint16_t num_slots;      ///< High water mark of slots (includes active and tombstoned slots)
    uint16_t free_space_end; ///< Byte offset from the start of the body marking end of free space
    uint32_t flags;          ///< Flag bits (reserved for page role, e.g. B+ Tree node flags)
    uint32_t checksum;       ///< Integrity check value (e.g. simple parity or XOR checksum)
};

/**
 * @struct Slot
 * @brief Denotes the physical location and byte size of a record in the page body.
 *
 * Offset = 0 and Length = 0 acts as a tombstone representing a deleted record slot.
 */
struct Slot {
    uint16_t offset; ///< Starting byte offset within the page body
    uint16_t length; ///< Length of the record payload in bytes (0 implies deleted)
};

/**
 * @brief Size of the writable page body after subtracting the fixed header footprint.
 */
constexpr size_t PAGE_BODY_SIZE = PAGE_SIZE - sizeof(PageHeader);

/**
 * @struct Page
 * @brief Fixed-size (4 KB) byte buffer representing a single disk/memory page.
 */
struct Page {
    PageHeader header;             ///< Fixed-size control header
    char body[PAGE_BODY_SIZE];     ///< Raw data storage block for slot list & record contents

    /**
     * @brief Formats a freshly allocated page's headers and clear payload memory.
     * @param pid The PageID to associate with this physical block.
     */
    void init(PageID pid) {
        std::memset(this, 0, sizeof(Page));
        header.page_id = pid;
        header.num_slots = 0;
        header.free_space_end = static_cast<uint16_t>(PAGE_BODY_SIZE);
        header.flags = 0;
        header.checksum = 0;
    }

    /**
     * @brief Computes remaining contiguous free space in bytes.
     * Calculated as the distance between the end of the slot array and the start of records.
     */
    uint16_t freeSpace() const {
        const uint16_t slot_array_size = static_cast<uint16_t>(header.num_slots * sizeof(Slot));
        return (header.free_space_end >= slot_array_size) ? (header.free_space_end - slot_array_size) : 0;
    }

    /**
     * @brief Retrieves a pointer to the slot array situated at the beginning of the body.
     */
    Slot* slotArray() {
        return reinterpret_cast<Slot*>(body);
    }

    /**
     * @brief Retrieves a const pointer to the slot array situated at the beginning of the body.
     */
    const Slot* slotArray() const {
        return reinterpret_cast<const Slot*>(body);
    }

    /**
     * @brief Fetches a reference to a specific slot by its SlotID.
     */
    Slot& slot(SlotID sid) {
        return slotArray()[sid];
    }

    /**
     * @brief Fetches a const reference to a specific slot by its SlotID.
     */
    const Slot& slot(SlotID sid) const {
        return slotArray()[sid];
    }

    /**
     * @brief Resolves a pointer to the start of a record's data payload.
     * @param sid The SlotID pointing to the target record.
     */
    char* recordData(SlotID sid) {
        return body + slotArray()[sid].offset;
    }

    /**
     * @brief Resolves a const pointer to the start of a record's data payload.
     * @param sid The SlotID pointing to the target record.
     */
    const char* recordData(SlotID sid) const {
        return body + slotArray()[sid].offset;
    }
};

// Validate compile-time constraint: Page must be exactly PAGE_SIZE bytes.
static_assert(sizeof(Page) == PAGE_SIZE, "Page structure footprint must equal PAGE_SIZE exactly");
