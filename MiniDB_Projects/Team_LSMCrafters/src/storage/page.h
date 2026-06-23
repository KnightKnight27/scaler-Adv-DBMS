#pragma once
#include <array>
#include <cstdint>
#include "common/config.h"
#include "common/types.h"

namespace minidb {

using LSN = uint64_t;            // log sequence number (see txn/wal.h)
constexpr LSN kInvalidLSN = 0;

// Header stored at the very start of every heap page.
struct PageHeader {
  PageId   next_page = kInvalidPage;  // next page in this table's chain
  uint16_t num_slots = 0;             // entries in the slot directory
  uint16_t free_ptr  = 0;             // offset where free space ends (tuples grow down)
  LSN      page_lsn  = kInvalidLSN;   // highest LSN applied to this page (redo guard)
  LSN      rec_lsn   = kInvalidLSN;   // first LSN that dirtied it (write-ahead rule)
};

// A fixed-size page: the unit of disk I/O. The raw bytes are interpreted by
// views such as SlottedPage; the fields below are in-memory bookkeeping only.
struct Page {
  PageId id        = kInvalidPage;
  bool   dirty     = false;
  int    pin_count = 0;
  std::array<char, PAGE_SIZE> data{};
};

}  // namespace minidb
