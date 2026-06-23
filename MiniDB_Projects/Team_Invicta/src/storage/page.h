#pragma once
#include <cstring>
#include "common/config.h"

namespace minidb {

// A Page is one fixed-size frame in the buffer pool. It owns a 4 KB byte buffer
// plus the bookkeeping the buffer pool needs: which on-disk page it holds, how
// many callers have it pinned, and whether it has been modified since load.
class Page {
 public:
  Page() { Reset(); }

  char *data() { return data_; }
  const char *data() const { return data_; }

  page_id_t page_id() const { return page_id_; }
  int pin_count() const { return pin_count_; }
  bool is_dirty() const { return is_dirty_; }

  void Reset() {
    std::memset(data_, 0, PAGE_SIZE);
    page_id_ = INVALID_PAGE_ID;
    pin_count_ = 0;
    is_dirty_ = false;
  }

 private:
  friend class BufferPoolManager;

  char      data_[PAGE_SIZE];
  page_id_t page_id_{INVALID_PAGE_ID};
  int       pin_count_{0};
  bool      is_dirty_{false};
};

}  // namespace minidb
