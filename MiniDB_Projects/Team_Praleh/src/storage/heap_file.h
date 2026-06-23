#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// heap_file.h  –  variable-length heap of fixed-size Record rows
// ─────────────────────────────────────────────────────────────────────────────
#include "storage/buffer_pool.h"
#include <vector>
#include <cstdint>

namespace minidb {

// ─── Row format ──────────────────────────────────────────────────────────────
// We keep rows simple: (int32 id, int32 value, uint8 deleted, 3-byte pad) = 12 B
struct Record {
    int32_t id      = 0;
    int32_t value   = 0;
    uint8_t deleted = 0;     // tombstone flag
    uint8_t _pad[3] = {};    // explicit padding → ABI-stable size
};
static_assert(sizeof(Record) == 12, "Record must be 12 bytes");

// ─── Page layout ─────────────────────────────────────────────────────────────
// Bytes 0–3  : next_page_id  (int32_t, INVALID_PAGE_ID if last page)
// Bytes 4–7  : num_records   (int32_t)
// Bytes 8–end: Record[0], Record[1], …
//
// MAX_RECORDS_PER_PAGE = (4096 - 8) / 12 = 340
// ─────────────────────────────────────────────────────────────────────────────

class HeapFile {
public:
    // first_page_id == INVALID_PAGE_ID → allocate a fresh first page.
    HeapFile(BufferPool* bp, page_id_t first_page_id);

    page_id_t           InsertRecord(const Record& rec);
    std::vector<Record> ScanAll()    const;
    bool                DeleteRecord(int32_t id);
    bool                SelectRecord(int32_t id, Record* out) const;

    page_id_t GetFirstPageId() const { return first_pid_; }

private:
    BufferPool* bp_;
    page_id_t   first_pid_;

    // Helpers to access the 8-byte header in a page's raw data.
    static page_id_t NextPage  (const char* d);
    static int32_t   NumRecords(const char* d);
    static void      SetNextPage  (char* d, page_id_t v);
    static void      SetNumRecords(char* d, int32_t   v);
    static       Record* Slot (char* d, int32_t i);
    static const Record* SlotC(const char* d, int32_t i);
};

} // namespace minidb
