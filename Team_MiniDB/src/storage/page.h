#pragma once
#include <cstdint>
#include <cstring>
#include <mutex>

namespace minidb {

using page_id_t = int32_t;
constexpr page_id_t INVALID_PAGE_ID = -1;
constexpr size_t PAGE_SIZE = 4096;

// A Page is a fixed-size in-memory frame that the buffer pool hands out.
// It owns no disk I/O logic itself - DiskManager does raw reads/writes,
// BufferPool decides which page_id occupies which frame.
class Page {
 public:
  Page() { ResetMemory(); }

  char *GetData() { return data_; }
  page_id_t GetPageId() const { return page_id_; }
  bool IsDirty() const { return is_dirty_; }
  int PinCount() const { return pin_count_; }

  void ResetMemory() { memset(data_, 0, PAGE_SIZE); }

  std::mutex &GetLatch() { return latch_; }

 private:
  friend class BufferPool;
  char data_[PAGE_SIZE]{};
  page_id_t page_id_ = INVALID_PAGE_ID;
  int pin_count_ = 0;
  bool is_dirty_ = false;
  std::mutex latch_;
};

}  // namespace minidb
