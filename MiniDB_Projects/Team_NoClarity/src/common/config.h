#ifndef CONFIG_H
#define CONFIG_H

#include <cstdint>
#include <cstddef>

namespace minidb {

using page_id_t = int32_t;
using frame_id_t = int32_t;
using lsn_t = int32_t;
using txn_id_t = int32_t;

static constexpr std::size_t PAGE_SIZE = 4096;
static constexpr page_id_t INVALID_PAGE_ID = -1;
static constexpr frame_id_t INVALID_FRAME_ID = -1;
static constexpr txn_id_t INVALID_TXN_ID = -1;

} // namespace minidb

#endif // CONFIG_H
