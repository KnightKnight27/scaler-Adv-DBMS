#pragma once
#include <cstdint>

// Global compile-time configuration for MiniDB.
namespace minidb {

// Size of a single page on disk and in the buffer pool, in bytes.
static const int PAGE_SIZE = 4096;

// Number of frames (pages) the buffer pool keeps resident in memory.
static const int BUFFER_POOL_FRAMES = 64;

// A page identifier is simply its 0-based index within the database file.
typedef int32_t PageId;

// A transaction identifier.
typedef int32_t TxnId;

static const PageId INVALID_PAGE_ID = -1;
static const TxnId  INVALID_TXN_ID  = -1;

// Default on-disk file names (relative to the working directory).
static const char DB_FILE_DEFAULT[]  = "minidb.db";
static const char WAL_FILE_DEFAULT[]  = "minidb.wal";

}  // namespace minidb
