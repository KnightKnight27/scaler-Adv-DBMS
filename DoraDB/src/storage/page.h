#pragma once

#include "common/config.h"
#include <cstdint>

// ============================================================
// Page — Slotted page layout for heap file storage
//
// Layout (4096 bytes total):
//
//   ┌───────────────────────────────┐  byte 0
//   │ PageHeader (16 bytes)         │
//   │   page_id       (4 bytes)    │
//   │   num_slots     (2 bytes)    │
//   │   free_space_ptr(2 bytes)    │ ← where free region starts
//   │   next_page_id  (4 bytes)    │ ← heap file linked list
//   │   flags         (2 bytes)    │
//   │   padding       (2 bytes)    │
//   ├───────────────────────────────┤  byte 16
//   │ Slot Directory               │
//   │   [offset, length] per slot  │ ← grows forward (4B each)
//   ├───────────────────────────────┤
//   │       Free Space              │
//   ├───────────────────────────────┤
//   │ Row Data (grows backward)     │ ← packed from end of page
//   └───────────────────────────────┘  byte 4095
//
// Slot entry = {offset (2B), length (2B)}
//   - Deleted slot: offset=0, length=0
//   - Forwarding: high bit of length set, data = target RID
// ============================================================

class Page {
public:
    // Initialize an empty page with the given ID
    void Init(int page_id);

    // Insert row_data (row_size bytes) into this page.
    // Returns slot_id on success, -1 if no space.
    int InsertRow(const char* row_data, int row_size);

    // Get row data at slot_id. Copies into out_data, sets out_size.
    // Returns false if slot is empty/deleted.
    bool GetRow(int slot_id, char* out_data, int* out_size) const;

    // Mark slot as deleted (tombstone). Returns false if already empty.
    bool DeleteRow(int slot_id);

    // Update row at slot_id with new data.
    // If new data fits in existing slot space, updates in-place.
    // If it doesn't fit, returns false (caller handles forwarding).
    bool UpdateRow(int slot_id, const char* new_data, int new_size);

    // ---- Header field accessors ----
    int GetPageId() const;
    void SetPageId(int id);

    int GetNumSlots() const;

    int GetNextPageId() const;
    void SetNextPageId(int next);

    int GetFreeSpace() const;

    // Raw data pointer — for DiskManager to read/write
    char* GetData() { return data_; }
    const char* GetData() const { return data_; }

private:
    char data_[PAGE_SIZE];

    // ---- Internal helpers ----

    // Get pointer to the slot entry at index i (in the slot directory)
    // Each slot entry is 4 bytes: [offset(2B), length(2B)]
    uint16_t GetSlotOffset(int slot_id) const;
    uint16_t GetSlotLength(int slot_id) const;
    void SetSlotEntry(int slot_id, uint16_t offset, uint16_t length);

    // How much free space is between slot directory end and row data start
    int GetFreeSpaceAmount() const;

    // Read/write header fields from data_ array
    uint16_t GetFreeSpacePtr() const;
    void SetFreeSpacePtr(uint16_t ptr);
    void SetNumSlots(uint16_t n);
};
