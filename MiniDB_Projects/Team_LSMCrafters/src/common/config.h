#pragma once
#include <cstddef>

// Compile-time knobs for MiniDB. They are kept small on purpose so the demos
// visibly exercise page allocation, buffer-pool eviction, and B+Tree splits.
namespace minidb {

constexpr std::size_t PAGE_SIZE     = 4096;  // bytes per page (unit of disk I/O)
constexpr int         BUFFER_FRAMES = 16;    // pages cached in the buffer pool
constexpr int         BTREE_ORDER   = 4;     // max children per B+Tree node

}  // namespace minidb
