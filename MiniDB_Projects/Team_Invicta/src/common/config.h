#pragma once
#include <cstdint>

// Global configuration constants and type aliases shared across MiniDB.
namespace minidb {

using page_id_t  = int32_t;   // logical id of a 4 KB page in the data file
using frame_id_t = int32_t;   // index of a frame inside the buffer pool
using slot_id_t  = int16_t;   // slot number within a slotted page
using txn_id_t   = int64_t;   // monotonically increasing transaction id
using lsn_t      = int64_t;   // log sequence number (WAL)

static constexpr int        PAGE_SIZE        = 4096;  // bytes per page
static constexpr int        BUFFER_POOL_SIZE = 64;    // frames in the buffer pool
static constexpr page_id_t  INVALID_PAGE_ID  = -1;
static constexpr txn_id_t   INVALID_TXN_ID   = -1;
static constexpr lsn_t      INVALID_LSN      = -1;

}  // namespace minidb
