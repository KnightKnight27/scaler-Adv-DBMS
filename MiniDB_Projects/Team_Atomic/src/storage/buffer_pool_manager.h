#pragma once
#include <unordered_map>
#include <list>
#include <mutex>
#include <vector>
#include "common/config.h"
#include "storage/page.h"
#include "storage/disk_manager.h"
#include "storage/lru_replacer.h"

namespace minidb {

// The buffer pool caches a fixed number of disk pages in memory. Callers
// pin a page (incrementing its pin count) while they use it, then unpin it.
// Only unpinned pages may be evicted, and dirty pages are written back to
// disk before their frame is reused.
class BufferPoolManager {
 public:
  BufferPoolManager(size_t pool_size, DiskManager* disk_manager);
  ~BufferPoolManager();

  // Fetch an existing page into the pool and pin it. nullptr if no frame free.
  Page* FetchPage(page_id_t page_id);

  // Allocate a brand-new page on disk, pin it, return it (id via *page_id).
  Page* NewPage(page_id_t* page_id);

  // Unpin a page; `is_dirty` marks it modified. Returns false if not pinned.
  bool UnpinPage(page_id_t page_id, bool is_dirty);

  // Write a single page back to disk (regardless of pin state).
  bool FlushPage(page_id_t page_id);

  // Flush every dirty page in the pool.
  void FlushAll();

  DiskManager* GetDiskManager() { return disk_manager_; }

 private:
  // Find a free frame, or evict an LRU victim (flushing if dirty).
  frame_id_t GetFreeFrame();

  size_t pool_size_;
  DiskManager* disk_manager_;
  std::vector<Page> frames_;                            // frame storage
  std::unordered_map<page_id_t, frame_id_t> page_table_;  // page_id -> frame
  std::list<frame_id_t> free_list_;                     // unused frames
  LRUReplacer replacer_;
  std::mutex latch_;
};

}  // namespace minidb
