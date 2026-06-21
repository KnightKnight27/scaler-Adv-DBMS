#pragma once

#include <cstddef>

// Global, compile-time configuration knobs for the engine.
// One page = one unit of disk I/O. 4096 bytes matches the OS page size
// (and SQLite's default), so one page read is one physical block read.
constexpr std::size_t PAGE_SIZE = 4096;

// Number of in-memory frames the buffer pool keeps. Small on purpose so the
// clock-sweep eviction path is easy to exercise and demo.
constexpr std::size_t DEFAULT_POOL_FRAMES = 16;
