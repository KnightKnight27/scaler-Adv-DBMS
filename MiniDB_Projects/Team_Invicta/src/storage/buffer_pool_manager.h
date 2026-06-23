#pragma once
#include <unordered_map>
#include <vector>
#include "storage/disk_manager.h"
#include "storage/lru_replacer.h"
#include "storage/page.h"

namespace minidb {

// The BufferPoolManager caches a fixed number of pages in memory and is the
// single choke point through which all page access flows. It pins pages while
// in use, evicts unpinned pages in LRU order when frames are scarce, and writes
// dirty pages back to disk before reusing their frame.
class BufferPoolManager {
 public:
  BufferPoolManager(DiskManager *disk, size_t pool_size = BUFFER_POOL_SIZE);

  // Fetch an existing page into the pool (pinned). Returns nullptr if no frame
  // is available (all frames pinned).
  Page *FetchPage(page_id_t page_id);

  // Allocate a brand-new page on disk and bring it into the pool (pinned).
  // Returns the page (with its id in *page_id) or nullptr if no frame is free.
  Page *NewPage(page_id_t *page_id);

  // Release a previously fetched page. `is_dirty` marks it modified.
  bool UnpinPage(page_id_t page_id, bool is_dirty);

  // Force a single page to disk (does not unpin).
  bool FlushPage(page_id_t page_id);

  // Force every resident page to disk (used on shutdown / before crash demo).
  void FlushAll();

  DiskManager *disk_manager() const { return disk_; }

 private:
  // Find a frame to use: a free one, or an LRU victim (flushed if dirty).
  bool GetVictimFrame(frame_id_t *frame);

  DiskManager                                *disk_;
  size_t                                      pool_size_;
  std::vector<Page>                           frames_;
  std::unordered_map<page_id_t, frame_id_t>   page_table_;
  std::vector<frame_id_t>                      free_list_;
  LRUReplacer                                 replacer_;
};

}  // namespace minidb
