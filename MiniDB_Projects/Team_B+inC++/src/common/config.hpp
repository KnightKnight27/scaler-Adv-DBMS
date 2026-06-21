#pragma once

#include <cstddef>

constexpr std::size_t PAGE_SIZE = 4096;  // one page = one disk I/O

constexpr std::size_t DEFAULT_POOL_FRAMES = 16;
