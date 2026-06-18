#pragma once

#include <cstring>
#include <shared_mutex>

#include "common/config.h"

namespace minidb {

// A Page is one fixed-size frame in the buffer pool. It carries the raw bytes
// plus the bookkeeping the buffer pool needs: which disk page it currently
// holds, how many callers have it pinned, and whether it has been modified
// since it was read in. A reader/writer latch protects the contents so higher
// layers (B+ tree, table heap) can coordinate concurrent access to one page.
class Page {
  friend class BufferPoolManager;

 public:
  Page() { ResetMemory(); }
  ~Page() = default;

  char *GetData() { return data_; }
  const char *GetData() const { return data_; }
  page_id_t GetPageId() const { return page_id_; }
  int GetPinCount() const { return pin_count_; }
  bool IsDirty() const { return is_dirty_; }

  lsn_t GetLSN() const { return lsn_; }
  void SetLSN(lsn_t lsn) { lsn_ = lsn; }

  void RLatch() { rwlatch_.lock_shared(); }
  void RUnlatch() { rwlatch_.unlock_shared(); }
  void WLatch() { rwlatch_.lock(); }
  void WUnlatch() { rwlatch_.unlock(); }

 private:
  void ResetMemory() { std::memset(data_, 0, PAGE_SIZE); }

  char data_[PAGE_SIZE]{};
  page_id_t page_id_{INVALID_PAGE_ID};
  int pin_count_{0};
  bool is_dirty_{false};
  lsn_t lsn_{INVALID_LSN};      // LSN of the last WAL record applied to this page
  std::shared_mutex rwlatch_;   // page-level reader/writer latch
};

}  // namespace minidb
