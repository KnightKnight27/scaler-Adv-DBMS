#pragma once
// storage/page.h — Fixed-size 4 KB slotted page.
//
// Binary layout of the data_[PAGE_SIZE] array:
//
//   ┌────────────────── HEADER (12 bytes) ──────────────────┐
//   │ page_id (4B) │ slot_count (2B) │ free_space_offset (2B) │ next_page_id (4B) │
//   └──────────────────────────────────────────────────────────┘
//   │                  Slot directory (grows →)                │
//   │  Each slot entry: offset (2B) + length (2B) = 4 bytes   │
//   │              ...                                         │
//   │──────────────── free space ─────────────────────────────│
//   │              ...                                         │
//   │           Tuple data (grows ← from end of page)         │
//   └─────────────────────────────────────────────────────────┘
//
// A slot with length == 0 is a tombstone (deleted tuple).

#include <cstdint>
#include <cstring>
#include <vector>
#include "common/types.h"

namespace minidb {

// ─── Header byte offsets ────────────────────────────────────
static constexpr uint16_t PAGE_HEADER_SIZE       = 12;   // bytes 0-11
static constexpr uint16_t SLOT_ENTRY_SIZE        = 4;    // 2B offset + 2B length

// ─── Page class ─────────────────────────────────────────────
class Page {
public:
    Page();

    // ── Page ID ──
    page_id_t get_page_id() const;
    void      set_page_id(page_id_t pid);

    // ── Slot count & free-space pointer ──
    uint16_t  get_slot_count() const;
    uint16_t  get_free_space_offset() const;

    // ── Linked-list pointer for overflow / free-page list ──
    page_id_t get_next_page_id() const;
    void      set_next_page_id(page_id_t pid);

    // ── Tuple operations ──
    // Insert raw bytes into the page.  Returns the assigned slot_id,
    // or INVALID_SLOT_ID if there is not enough room.
    slot_id_t insert_tuple(const char* data, uint16_t length);

    // Tombstone a slot (set its length to 0).
    bool delete_tuple(slot_id_t slot_id);

    // Read a tuple's raw bytes into *out (caller must provide enough space).
    // Writes the actual length to *out_length.
    bool get_tuple(slot_id_t slot_id, char* out, uint16_t* out_length) const;

    // How many payload bytes can still fit (accounting for a new slot entry).
    uint16_t get_free_space() const;

    // Raw pointer to the 4 KB buffer — used by disk I/O routines.
    char*       get_data();
    const char* get_data() const;

    // Compact: rewrite tuple data to remove gaps left by tombstoned slots.
    // Live slot offsets are updated; tombstoned slots are preserved as-is (length 0).
    void compact();

private:
    char data_[PAGE_SIZE];

    // ── Internal helpers to read/write header fields ──
    void     set_slot_count(uint16_t count);
    void     set_free_space_offset(uint16_t offset);

    // Read/write a slot directory entry (0-indexed).
    void     get_slot(slot_id_t sid, uint16_t* offset, uint16_t* length) const;
    void     set_slot(slot_id_t sid, uint16_t offset, uint16_t length);
};

// ─── Tuple Serialization ────────────────────────────────────
// Encode a logical Tuple into a byte buffer, guided by the Schema.
// Format per column:
//   1-byte null flag  (0 = not null, 1 = null)
//   if not null:
//     INT     → 4 bytes (little-endian memcpy)
//     FLOAT   → 8 bytes (double memcpy)
//     BOOL    → 1 byte
//     VARCHAR → 2-byte length prefix + chars
std::vector<char> serialize_tuple(const Tuple& tuple, const Schema& schema);

// Decode raw bytes back into a Tuple.
Tuple deserialize_tuple(const char* data, uint16_t length, const Schema& schema);

}  // namespace minidb
