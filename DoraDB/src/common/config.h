#pragma once

// ============================================================
// DoraDB Configuration Constants
// ============================================================

// Page size in bytes (4 KB — standard for most databases)
constexpr int PAGE_SIZE = 4096;

// Number of frames in the buffer pool
constexpr int BUFFER_POOL_SIZE = 64;

// Sentinel for "no page" / invalid page ID
constexpr int INVALID_PAGE_ID = -1;

// Page header size in bytes
constexpr int PAGE_HEADER_SIZE = 16;

// Slot entry size: {offset (2B), length (2B)}
constexpr int SLOT_SIZE = 4;

// Directory where database files are stored
constexpr const char* DATA_DIR = "data";
