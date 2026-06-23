#pragma once
#include <list>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "disk_manager.h"
#include "page.h"

namespace minidb {

// Fixed-size pool of frames backed by DiskManager. Replacement policy is
// LRU among currently unpinned frames. This is the only place page faults
// happen; everything above (heap files, B+Tree) goes through here so that
// "buffer pool usage" is centralized and demonstrable.
class BufferPool {
 public:
  BufferPool(size_t pool_size, DiskManager *disk_manager);
  ~BufferPool();

  Page *FetchPage(page_id_t page_id);
  Page *NewPage(page_id_t *page_id);
  bool UnpinPage(page_id_t page_id, bool is_dirty);
  bool FlushPage(page_id_t page_id);
  void FlushAllPages();
  bool DeletePage(page_id_t page_id);

  size_t PoolSize() const { return pool_size_; }

 private:
  bool FindFrameToEvict(size_t *frame_id);
  void TouchLru(size_t frame_id);

  size_t pool_size_;
  DiskManager *disk_manager_;
  std::vector<Page> pages_;
  std::unordered_map<page_id_t, size_t> page_table_;  // page_id -> frame_id
  std::list<size_t> free_list_;                       // frames never used
  std::list<size_t> lru_list_;                         // MRU front, LRU back
  std::mutex latch_;
};

}  // namespace minidb
