#pragma once

#include <cstring>

#include "common/config.h"

namespace walterdb {

// ---------------------------------------------------------------------------
// Page -- one in-memory frame of the buffer pool.
//
// It is a fixed PAGE_SIZE byte buffer plus the bookkeeping the pool needs:
//   * page_id   -- which on-disk page currently occupies this frame
//   * pin_count -- how many callers are actively using it (cannot be evicted
//                  while > 0)
//   * is_dirty  -- modified in memory and not yet written back
//
// Higher layers never see frames directly; they get a Page* from the pool,
// reinterpret data() as a SlottedPage / B+tree node, and hand it back with
// unpin_page().  Only BufferPool mutates the bookkeeping fields.
// ---------------------------------------------------------------------------

class Page {
 public:
  char* data() { return data_; }
  const char* data() const { return data_; }

  page_id_t page_id() const { return page_id_; }
  int pin_count() const { return pin_count_; }
  bool is_dirty() const { return is_dirty_; }

 private:
  friend class BufferPool;

  void reset(page_id_t pid) {
    page_id_ = pid;
    pin_count_ = 0;
    is_dirty_ = false;
    std::memset(data_, 0, PAGE_SIZE);
  }

  alignas(8) char data_[PAGE_SIZE] = {};
  page_id_t page_id_ = INVALID_PAGE_ID;
  int pin_count_ = 0;
  bool is_dirty_ = false;
};

}  // namespace walterdb
