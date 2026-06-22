#pragma once
#include <cstddef>

// Global, compile-time configuration knobs for the engine.
namespace minidb {

// Every page on disk and in the buffer pool is exactly this many bytes.
constexpr size_t PAGE_SIZE = 4096;

// Number of frames the buffer pool keeps resident in memory. Kept small on
// purpose so that the clock-sweep eviction path is actually exercised.
constexpr size_t BUFFER_POOL_FRAMES = 64;

} // namespace minidb
