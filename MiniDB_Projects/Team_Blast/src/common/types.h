#pragma once

#include <cstdint>
#include <cstddef>

// ─── Page and Record identity types ───────────────────────────────────────────

// A PageID uniquely identifies a page within a database file.
// PageID 0 is the metadata/header page; data pages start from 1.
using PageID = uint32_t;

// Invalid sentinel for PageID
constexpr PageID INVALID_PAGE_ID = UINT32_MAX;

// A SlotID is an index into the slot array of a slotted page.
using SlotID = uint16_t;

// Invalid sentinel for SlotID
constexpr SlotID INVALID_SLOT_ID = UINT16_MAX;

// A RecordID uniquely identifies a record: which page and which slot.
struct RecordID {
    PageID page_id  = INVALID_PAGE_ID;
    SlotID slot_id  = INVALID_SLOT_ID;

    bool isValid() const {
        return page_id != INVALID_PAGE_ID && slot_id != INVALID_SLOT_ID;
    }

    bool operator==(const RecordID& other) const {
        return page_id == other.page_id && slot_id == other.slot_id;
    }

    bool operator!=(const RecordID& other) const {
        return !(*this == other);
    }
};

// ─── Transaction identity ─────────────────────────────────────────────────────

// TxID uniquely identifies a transaction in this session.
// Starts from 1; 0 means "no transaction" (system operations).
using TxID = uint64_t;

constexpr TxID INVALID_TX_ID = 0;

// ─── Log Sequence Number ───────────────────────────────────────────────────────

// LSN uniquely identifies a WAL log record.
// Always increasing; used for recovery and replication.
using LSN = uint64_t;

constexpr LSN INVALID_LSN = 0;
