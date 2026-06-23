#pragma once

// Config.hpp - Global constants for MiniDB.
// We use fixed-size pages because real databases align with disk block size (usually 4KB).

namespace minidb {

// Each page is exactly 4096 bytes (4 KB).
constexpr int PAGE_SIZE = 4096;

// Maximum number of pages kept in the buffer pool at once.
constexpr int BUFFER_POOL_CAPACITY = 32;

// B+ Tree fan-out: max keys per node (kept small for easy debugging).
constexpr int BTREE_ORDER = 4;

// Track A extension: process this many rows per batch.
constexpr int BATCH_SIZE = 64;

// Folder where database files are stored.
constexpr const char* DATA_DIR = "minidb_data";

}  // namespace minidb
