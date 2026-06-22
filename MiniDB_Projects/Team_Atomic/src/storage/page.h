#pragma once
#include <cstring>
#include "common/config.h"

namespace minidb {

// One buffer-pool frame: a page of raw bytes plus bookkeeping.
class Page {
 public:
  Page() { Reset(); }

  char* Data() { return data_; }
  const char* Data() const { return data_; }

  page_id_t GetPageId() const { return page_id_; }
  int GetPinCount() const { return pin_count_; }
  bool IsDirty() const { return is_dirty_; }

  void Reset() {
    std::memset(data_, 0, PAGE_SIZE);
    page_id_ = INVALID_PAGE_ID;
    pin_count_ = 0;
    is_dirty_ = false;
  }

 private:
  friend class BufferPoolManager;

  char data_[PAGE_SIZE];
  page_id_t page_id_ = INVALID_PAGE_ID;
  int pin_count_ = 0;
  bool is_dirty_ = false;
};

}  // namespace minidb
