#pragma once

#include <cstddef>

// ─── Storage configuration ────────────────────────────────────────────────────

// Size of each database page in bytes.
// 4096 bytes = 4 KB, matching SQLite's default page size.
// All pages on disk and in the buffer pool are exactly this size.
constexpr size_t PAGE_SIZE = 4096;

// Number of frames in the buffer pool.
// At most this many pages can be in memory at the same time.
constexpr size_t BUFFER_POOL_SIZE = 64;

// Maximum number of bytes a single record's data field can hold.
// Keeps our slotted page layout simple.
constexpr size_t MAX_RECORD_SIZE = 256;

// Maximum length of a table name string.
constexpr size_t MAX_TABLE_NAME_LEN = 32;

// ─── B+ Tree configuration ────────────────────────────────────────────────────

// Order of the B+ tree.
// Internal nodes hold up to ORDER-1 keys and ORDER children.
// Leaf nodes hold up to ORDER-1 key-value pairs.
// ORDER=4 means max 3 keys per node — keeps splits easy to demonstrate.
constexpr int BTREE_ORDER = 4;

// ─── WAL configuration ────────────────────────────────────────────────────────

// Maximum size (bytes) of each WAL record's value field.
constexpr size_t WAL_MAX_VALUE_SIZE = 256;

// ─── Replication configuration ────────────────────────────────────────────────

// How often (milliseconds) the replica polls for new log records.
constexpr int REPLICA_POLL_MS = 100;

// ─── Cost model ───────────────────────────────────────────────────────────────

// Cost (abstract units) to perform one page I/O.
constexpr double IO_PAGE_COST = 1.0;
