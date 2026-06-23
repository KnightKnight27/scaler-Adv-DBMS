#pragma once

#include "common/types.h"
#include <cstring>
#include <vector>

namespace minidb {

/*
 * Page layout (4 KB):
 * ┌───────────────────────────────────────┐
 * │  Header (8 bytes)                     │
 * │    num_records (uint16_t)             │
 * │    free_offset (uint16_t)             │
 * │    next_page   (page_id_t)            │
 * ├───────────────────────────────────────┤
 * │  Slot directory (grows downward)      │
 * │    [offset, length, valid] per slot   │
 * ├───────────────────────────────────────┤
 * │  Free space                           │
 * ├───────────────────────────────────────┤
 * │  Record data (grows upward from end)  │
 * └───────────────────────────────────────┘
 */

// Slot entry in the directory
struct SlotEntry {
    uint16_t offset;   // byte offset of record data within page
    uint16_t length;   // length of record data
    bool     valid;    // false = deleted
};

class Page {
public:
    Page();

    // Reset page to empty state
    void Init(page_id_t pid);

    page_id_t GetPageId() const { return page_id_; }
    void      SetPageId(page_id_t pid) { page_id_ = pid; }

    // Insert a serialized record. Returns slot index, or -1 if no space.
    int InsertRecord(const char* data, uint16_t len);

    // Get a record by slot index. Returns false if slot invalid.
    bool GetRecord(slot_id_t slot, std::vector<char>& out) const;

    // Delete record at slot (marks slot invalid, does NOT reclaim space).
    bool DeleteRecord(slot_id_t slot);

    // True when a record and its slot entry fit on this page.
    bool HasSpace(uint16_t record_size) const;

    uint16_t GetNumRecords() const { return num_records_; }
    uint16_t GetNumSlots()   const { return static_cast<uint16_t>(slots_.size()); }

    // Raw data access for disk I/O
    const char* GetRawData() const { return data_; }
    char*       GetMutableData()   { return data_; }

    // Serialize/deserialize the page to/from the raw data_ buffer
    void SerializeHeader();
    void DeserializeHeader();

    // Next page pointer for heap file linked list
    page_id_t GetNextPageId() const { return next_page_id_; }
    void      SetNextPageId(page_id_t pid) { next_page_id_ = pid; }

private:
    static constexpr uint16_t HEADER_SIZE = 8; // num_records(2) + free_offset(2) + next_page(4)

    char       data_[PAGE_SIZE];
    page_id_t  page_id_      = INVALID_PAGE_ID;
    uint16_t   num_records_  = 0;    // count of valid (non-deleted) records
    uint16_t   free_offset_  = HEADER_SIZE; // next free byte after header+slots
    page_id_t  next_page_id_ = INVALID_PAGE_ID;
    std::vector<SlotEntry> slots_;

    uint16_t FreeSpace() const;
};

} // namespace minidb
