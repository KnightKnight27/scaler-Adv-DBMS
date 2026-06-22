#pragma once
#include <cstdint>

namespace minidb {

// ---- Global engine configuration ----
// Page size in bytes. 4 KB is the classic OS-page-aligned choice.
static constexpr int PAGE_SIZE = 4096;

// Number of frames in the buffer pool.
static constexpr int BUFFER_POOL_SIZE = 128;

// Identifier types.
using page_id_t  = int32_t;   // index of a page within the DB file
using frame_id_t = int32_t;   // index of a frame within the buffer pool
using slot_id_t  = int32_t;   // slot index within a heap page
using txn_id_t   = int64_t;   // transaction id
using lsn_t      = int64_t;   // log sequence number

static constexpr page_id_t INVALID_PAGE_ID = -1;
static constexpr txn_id_t  INVALID_TXN_ID  = -1;
static constexpr lsn_t     INVALID_LSN     = -1;

}  // namespace minidb
