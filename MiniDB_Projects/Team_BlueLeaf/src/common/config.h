#pragma once

#include <cstddef>

namespace minidb {

// Number of in-memory frames in the buffer pool. Small by default so that
// demos and tests actually exercise eviction; tunable per BufferPool instance.
constexpr std::size_t DEFAULT_BUFFER_POOL_FRAMES = 64;

// Clock-sweep usage-count cap. PostgreSQL caps usage_count at 5; a higher cap
// makes hot pages "stickier" but slows the sweep. (See lab_3 / buffer pool notes.)
constexpr int MAX_USAGE_COUNT = 5;

} // namespace minidb
