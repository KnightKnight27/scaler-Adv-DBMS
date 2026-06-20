#pragma once
#include <cstdint>
#include <cstddef>

// ---------------------------------------------------------------------------
// MiniDB global configuration constants.
// Keeping these in one place makes the engine's tunables easy to explain.
// ---------------------------------------------------------------------------
namespace minidb {

// Size of a single page on disk and in the buffer pool. 4 KB matches the
// classic OS page size and keeps disk I/O aligned.
constexpr size_t PAGE_SIZE = 4096;

// Number of frames in the buffer pool. Small on purpose so that eviction
// (and therefore the replacement policy) is easy to demonstrate.
constexpr size_t BUFFER_POOL_SIZE = 64;

// Sentinel values.
constexpr int32_t INVALID_PAGE_ID = -1;
constexpr int32_t INVALID_FRAME_ID = -1;
constexpr uint64_t INVALID_TXN_ID = 0;
constexpr uint64_t INVALID_LSN = 0;

} // namespace minidb
