#pragma once

#include <cstddef>
#include <cstdint>

// ---------------------------------------------------------------------------
// Global type aliases and tunable constants for AxiomDB.
//
// Everything that the whole engine agrees on -- the on-disk page size, the
// width of a page id, what "invalid" looks like -- lives here so there is a
// single source of truth.  Changing PAGE_SIZE in one place reflows the entire
// storage layer.
// ---------------------------------------------------------------------------

namespace axiomdb {

// Identifier widths.  Signed so that the "invalid" sentinel (-1) is natural and
// so that arithmetic never silently wraps.
using page_id_t = int32_t;   // index of a page within a file (0, 1, 2, ...)
using frame_id_t = int32_t;  // index of a frame within the buffer pool
using slot_id_t = uint16_t;  // index of a slot within a slotted page
using txn_id_t = int64_t;    // monotonically increasing transaction id
using lsn_t = int64_t;       // log sequence number (offset into the WAL stream)

// On-disk page size.  4 KiB matches a typical OS page / SSD sector grouping and
// is the size the lecture material uses for slotted pages.
inline constexpr size_t PAGE_SIZE = 4096;

// Sentinels.
inline constexpr page_id_t INVALID_PAGE_ID = -1;
inline constexpr frame_id_t INVALID_FRAME_ID = -1;
inline constexpr txn_id_t INVALID_TXN_ID = -1;
inline constexpr lsn_t INVALID_LSN = -1;

// Buffer pool default capacity (number of in-memory frames).  Small by default
// so that eviction is easy to exercise in tests and demos.
inline constexpr size_t BUFFER_POOL_DEFAULT_FRAMES = 256;

}  // namespace axiomdb
