#pragma once

#include <cstdint>

namespace minidb {

// ---------------------------------------------------------------------------
// Global engine constants and type aliases.
// Everything in MiniDB is organised around a fixed-size *page*; this header
// fixes that size and the small integer id types used throughout the engine.
// ---------------------------------------------------------------------------

static constexpr int PAGE_SIZE = 4096;            // bytes per page (disk + buffer frame)
static constexpr int BUFFER_POOL_SIZE = 128;      // default frames in the buffer pool

using page_id_t = int32_t;    // identifies a page within a file (offset = id * PAGE_SIZE)
using frame_id_t = int32_t;   // identifies a frame (slot) in the buffer pool
using txn_id_t = int32_t;     // transaction id
using lsn_t = int32_t;        // log sequence number (WAL)
using slot_id_t = uint16_t;   // slot number within a slotted page

static constexpr page_id_t INVALID_PAGE_ID = -1;
static constexpr frame_id_t INVALID_FRAME_ID = -1;
static constexpr txn_id_t INVALID_TXN_ID = -1;
static constexpr lsn_t INVALID_LSN = -1;

}  // namespace minidb
