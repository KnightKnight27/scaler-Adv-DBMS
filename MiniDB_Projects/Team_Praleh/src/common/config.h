#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// config.h  –  compile-time constants for MiniDB
// ─────────────────────────────────────────────────────────────────────────────
#include <cstdint>
#include <cstddef>

namespace minidb {

// Every page on disk / in memory is exactly 4 KB.
inline constexpr uint32_t PAGE_SIZE        = 4096;

// Sentinel value: "no valid page".
inline constexpr int32_t  INVALID_PAGE_ID  = -1;

// Number of frames the buffer pool keeps in RAM.
inline constexpr size_t   DEFAULT_POOL_SIZE = 64;

// B+ Tree order: max children per internal node.
inline constexpr int      BTREE_ORDER      = 4;

// File names used at runtime (relative to CWD).
inline constexpr const char* DB_FILE       = "minidb.db";
inline constexpr const char* WAL_FILE      = "minidb.wal";
inline constexpr const char* REPLICA_FILE  = "minidb_replica.db";
inline constexpr const char* REPLICA_LOG   = "minidb_replication.log";

} // namespace minidb
