#pragma once

#include <cstdint>

namespace minidb {

constexpr int32_t PAGE_SIZE = 4096;               // size of a data page in bytes
constexpr int32_t BUFFER_POOL_SIZE = 16;          // number of frames in buffer pool
constexpr int32_t INVALID_PAGE_ID = -1;
constexpr int64_t INVALID_LSN = -1;
constexpr int32_t INVALID_TXN_ID = -1;

} // namespace minidb
