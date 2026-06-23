#pragma once

#include <cstdint>
#include <string>

#include "common/types.h"

namespace minidb {

// SlottedPage is a *view* over a Page's 4 KiB byte buffer that lays out
// variable-length records, exactly the structure taught in the storage-engine
// lectures: [ header | slot array -> | <- free space | records ].
//
// Byte layout (all integers stored in host byte order; single-machine DB):
//
//   byte 0  : uint32 checksum      CRC32 over bytes [4, PAGE_SIZE). Stamped by the
//                                  DiskManager on write, verified on read. Kept first
//                                  so it is page-type-agnostic (heap and B+Tree pages
//                                  both reserve bytes [0,4) for it).
//   byte 4  : uint64 page_lsn      ARIES pageLSN (used from M5; 0 until then).
//   byte 12 : int32  next_page_id  Heap-file page chain; INVALID_PAGE_ID if last.
//   byte 16 : uint16 slot_count    Number of slots (live + erased).
//   byte 18 : uint16 free_ptr      Offset where the record region currently starts.
//   byte 20 : slot directory, growing UP. Slot i = { uint16 offset, uint16 length }.
//             An erased/dead slot has offset==0 && length==0.
//   ...     : free space
//   records : grow DOWN from PAGE_SIZE.
//
// Free space = free_ptr - (HEADER_SIZE + slot_count * SLOT_SIZE).
class SlottedPage {
public:
    static constexpr std::uint16_t HEADER_SIZE = 20;
    static constexpr std::uint16_t SLOT_SIZE   = 4;  // 2-byte offset + 2-byte length

    explicit SlottedPage(char* data) : data_(data) {}

    // Prepare a freshly allocated page for use (empty, no records).
    void init();

    // --- header accessors ---
    lsn_t  page_lsn() const;
    void   set_page_lsn(lsn_t lsn);
    PageId next_page() const;
    void   set_next_page(PageId id);
    std::uint16_t slot_count() const;

    // --- record operations ---
    // Insert a record; on success writes the new slot index to out_slot and
    // returns true. Returns false only if the record cannot fit even after
    // compacting away erased records.
    bool insert(const char* rec, std::uint16_t len, std::int16_t& out_slot);

    // Read a live record into `out`. Returns false if the slot is out of range
    // or has been erased.
    bool get(std::int16_t slot, std::string& out) const;

    // Mark a slot's record as erased. Space is reclaimed lazily by compact().
    bool erase(std::int16_t slot);

    // Bytes available for a new record (after accounting for one new slot).
    std::uint16_t free_space() const;

private:
    // Slot directory entry.
    struct Slot { std::uint16_t offset; std::uint16_t length; };

    Slot slot_at(std::int16_t i) const;
    void set_slot(std::int16_t i, Slot s);
    void set_slot_count(std::uint16_t n);
    std::uint16_t free_ptr() const;
    void set_free_ptr(std::uint16_t p);

    // Reclaim space from erased records by sliding live records together.
    void compact();

    char* data_;
};

} // namespace minidb
