#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// types.h  –  project-wide type aliases
// ─────────────────────────────────────────────────────────────────────────────
#include <cstdint>

namespace minidb {

using page_id_t = int32_t;  // disk page index (0-based)
using txn_id_t = int32_t;   // transaction identifier
using lsn_t = int32_t;      // log sequence number (WAL)
using frame_id_t = int32_t; // buffer-pool frame index

} // namespace minidb
