#pragma once

#include "buffer_pool.h"
#include "storage.h"

#include <cstdint>
#include <limits>

// ─── Fixed schema: three 64-bit integers ─────────────────────────────────────

struct Tuple {
    int64_t id{0};
    int64_t val1{0};
    int64_t val2{0};
};
static_assert(sizeof(Tuple) == 24);

// Sentinel stored in a soft-deleted tuple's id field.  Chosen as INT64_MIN so
// it never collides with real data; ScaSeqScan and BatchSeqScan skip slots that
// carry this value.
static constexpr int64_t DELETED_TUPLE_ID = std::numeric_limits<int64_t>::min();

// ─── Record Identifier ────────────────────────────────────────────────────────

struct RID {
    page_id_t page_id{INVALID_PAGE_ID};
    int32_t   slot_num{-1};
    bool IsValid() const { return page_id != INVALID_PAGE_ID && slot_num >= 0; }
};

// ─── Heap page layout ─────────────────────────────────────────────────────────
// [int32_t num_tuples | int32_t next_page_id | Tuple tuples[170]]
// 4 + 4 + 170 * 24 = 4088 ≤ 4096  ✓

static constexpr int32_t HEAP_PAGE_TUPLES =
    static_cast<int32_t>((PAGE_SIZE - 2 * sizeof(int32_t)) / sizeof(Tuple));  // 170

struct HeapPage {
    int32_t   num_tuples;
    page_id_t next_page_id;
    Tuple     tuples[HEAP_PAGE_TUPLES];
};
static_assert(sizeof(HeapPage) <= PAGE_SIZE);

// ─── TableHeap ───────────────────────────────────────────────────────────────

class TableHeap {
public:
    explicit TableHeap(BufferPoolManager *bpm);

    RID  InsertTuple(const Tuple &t);
    bool GetTuple(RID rid, Tuple *out) const;

    // Soft-delete: overwrites the slot with DELETED_TUPLE_ID and marks the page
    // dirty.  The slot number is not reclaimed; SeqScan skips tombstones.
    void DeleteTuple(RID rid);

    BufferPoolManager *GetBPM()         const { return bpm_; }
    page_id_t          GetFirstPageId() const { return first_page_id_; }
    int32_t            GetNumTuples()   const { return num_tuples_; }

private:
    BufferPoolManager *bpm_;
    page_id_t          first_page_id_{INVALID_PAGE_ID};
    page_id_t          last_page_id_ {INVALID_PAGE_ID};
    int32_t            num_tuples_   {0};   // count of live (non-deleted) tuples
};
