#pragma once

#include "common/types.h"
#include "common/config.h"
#include <cstdint>
#include <cstring>

// ─── Page Header ──────────────────────────────────────────────────────────────
//
// Every page starts with this fixed-size header.
// After the header comes the slot array (growing forward) and record data
// (growing backward from the end of the page). This is the classic
// "slotted page" layout used in real databases like PostgreSQL.
//
// Layout diagram:
//
//  ┌────────────────────────────────────────────────────────────────┐
//  │  PageHeader (fixed)                                            │
//  ├────────────────────────────────────────────────────────────────┤
//  │  Slot[0]  Slot[1]  ...  Slot[num_slots-1]   (grows →)         │
//  ├──────────────────────────────────┬─────────────────────────────┤
//  │           free space             │                             │
//  ├──────────────────────────────────┘                             │
//  │                  record data (grows ←)                         │
//  └────────────────────────────────────────────────────────────────┘
//
// free_space_end points to the start of the next record written from the back.
// Slots hold offsets into the page body where each record lives.

struct PageHeader {
    PageID   page_id;         // which page this is (written when page is allocated)
    uint16_t num_slots;       // number of slots (including deleted slots)
    uint16_t free_space_end;  // offset (from start of page body) of the next free byte at the bottom
    uint32_t flags;           // reserved for future use (e.g., leaf/internal node type)
    uint32_t checksum;        // simple XOR checksum for corruption detection
};

// ─── Slot entry ───────────────────────────────────────────────────────────────
// Each slot stores the offset + length of a record in the page body.
// offset = 0 and length = 0 means the slot is deleted (tombstone).
struct Slot {
    uint16_t offset;   // byte offset from start of page body where record data begins
    uint16_t length;   // number of bytes in the record (0 = deleted)
};

// ─── Page ─────────────────────────────────────────────────────────────────────
// A page is exactly PAGE_SIZE bytes. The header sits at the front.
// The rest ("body") is used for slots and record data.
//
// We use a raw char array so we can control layout precisely.
// This matches how real databases handle pages — no hidden padding.

constexpr size_t PAGE_BODY_SIZE = PAGE_SIZE - sizeof(PageHeader);

struct Page {
    PageHeader header;
    char       body[PAGE_BODY_SIZE];  // slots + record data live here

    // ── Helpers ──

    // Initialize a freshly allocated page.
    void init(PageID pid) {
        std::memset(this, 0, sizeof(Page));
        header.page_id        = pid;
        header.num_slots      = 0;
        // free_space_end starts at the end of the body (records grow backward)
        header.free_space_end = static_cast<uint16_t>(PAGE_BODY_SIZE);
        header.flags          = 0;
        header.checksum       = 0;
    }

    // How many bytes of usable free space remain in this page?
    // Free space = gap between end of slot array and start of record data area.
    uint16_t freeSpace() const {
        uint16_t slot_array_end = static_cast<uint16_t>(header.num_slots * sizeof(Slot));
        return header.free_space_end - slot_array_end;
    }

    // Pointer to the slot array at the front of the body.
    Slot* slotArray() {
        return reinterpret_cast<Slot*>(body);
    }
    const Slot* slotArray() const {
        return reinterpret_cast<const Slot*>(body);
    }

    // Pointer to a specific slot.
    Slot& slot(SlotID sid) {
        return slotArray()[sid];
    }
    const Slot& slot(SlotID sid) const {
        return slotArray()[sid];
    }

    // Pointer to the record data for a given slot.
    char* recordData(SlotID sid) {
        return body + slotArray()[sid].offset;
    }
    const char* recordData(SlotID sid) const {
        return body + slotArray()[sid].offset;
    }
};

// Ensure page is exactly PAGE_SIZE bytes — catches miscalculations at compile time.
static_assert(sizeof(Page) == PAGE_SIZE, "Page struct must be exactly PAGE_SIZE bytes");
