#pragma once
// Fixed-size buffer pool with LRU replacement over a PageManager.
// Callers fetch a pinned page, use its bytes, then unpin (marking dirty if written).
#include "minidb/storage/page.hpp"
#include "minidb/storage/page_manager.hpp"
#include <array>
#include <cstdint>
#include <list>
#include <unordered_map>
#include <vector>

namespace minidb {

class BufferPool {
 public:
  BufferPool(PageManager& pm, size_t num_frames);

  uint8_t* fetch_page(PageId id);          // pins and returns the page buffer
  PageId new_page(uint8_t*& out);          // allocates, pins, returns new page id
  void unpin(PageId id, bool dirty);
  void flush_all();

  size_t hits() const { return hits_; }
  size_t misses() const { return misses_; }

 private:
  struct Frame {
    PageId page_id = kInvalidPageId;
    int pin = 0;
    bool dirty = false;
    std::array<uint8_t, kPageSize> data{};
  };

  size_t acquire_frame();  // a free or evictable frame index
  void lru_remove(size_t idx);
  void lru_add(size_t idx);

  PageManager& pm_;
  std::vector<Frame> frames_;
  std::unordered_map<PageId, size_t> table_;          // page_id -> frame index
  std::vector<size_t> free_;                          // unused frames
  std::list<size_t> lru_;                             // front = least recent
  std::unordered_map<size_t, std::list<size_t>::iterator> lru_pos_;
  size_t hits_ = 0;
  size_t misses_ = 0;
};

}  // namespace minidb
