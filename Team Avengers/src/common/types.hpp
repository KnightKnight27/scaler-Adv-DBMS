// ============================================================================
//  types.hpp — Core type aliases and engine-wide constants for MiniDB
//
//  Everything below the SQL layer speaks in these primitive ids. Keeping them
//  in one place means a page id is always the same width everywhere, and makes
//  the "what is INVALID" question have exactly one answer per id type.
// ============================================================================
#pragma once

#include <cstdint>
#include <cstddef>

namespace minidb {

// --- on-disk geometry -------------------------------------------------------
// A page is the unit of transfer between disk and the buffer pool. 4 KiB is the
// classic choice: it matches the OS page size, so a read never straddles two
// kernel pages. Every heap file and every B+tree node is exactly one page.
constexpr int PAGE_SIZE = 4096;

// How many pages the buffer pool keeps resident at once. Small on purpose so we
// can *force* eviction in a demo with only a handful of pages.
constexpr size_t BUFFER_POOL_SIZE = 64;

// --- id types ---------------------------------------------------------------
using page_id_t  = int32_t;   // index of a page within a heap/index file
using frame_id_t = int32_t;   // slot within the buffer pool's frame array
using txn_id_t   = int32_t;   // monotonically increasing transaction id
using lsn_t      = int64_t;   // log sequence number (offset into the WAL)
using timestamp_t = int64_t;  // logical clock value, used for MVCC snapshots

// --- sentinels --------------------------------------------------------------
// A negative id can never be a real array index, so it doubles as "none".
constexpr page_id_t  INVALID_PAGE_ID  = -1;
constexpr frame_id_t INVALID_FRAME_ID = -1;
constexpr txn_id_t   INVALID_TXN_ID   = -1;
constexpr lsn_t      INVALID_LSN       = -1;

// A RID (record id) uniquely locates a tuple: which page, which slot on it.
// This is the "physical address" the B+tree index points at.
struct RID {
    page_id_t page_id = INVALID_PAGE_ID;
    uint16_t  slot_num = 0;

    bool operator==(const RID& o) const {
        return page_id == o.page_id && slot_num == o.slot_num;
    }
    bool operator!=(const RID& o) const { return !(*this == o); }
    bool valid() const { return page_id != INVALID_PAGE_ID; }
};

}  // namespace minidb
