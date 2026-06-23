#pragma once
#include <cstdint>
#include <unordered_map>
#include <vector>
#include "storage/disk_manager.h"
#include "storage/log_flusher.h"
#include "storage/page.h"

namespace minidb {

// Caches a fixed number of pages in memory and evicts with the clock-sweep
// policy (an approximation of LRU using a per-frame usage counter). Callers
// must pair every fetch_page with an unpin_page; pinned frames are never
// evicted.
class BufferPool {
 public:
  explicit BufferPool(DiskManager& disk);

  Page* fetch_page(PageId id);              // returns a pinned page (loads on miss)
  void  unpin_page(PageId id, bool dirty);  // release one pin; OR in the dirty flag
  PageId allocate_page();                   // grow the data file by one page
  void  flush_page(PageId id);
  void  flush_all();

  // Attaching a flusher enables the write-ahead rule on dirty-page eviction.
  void  set_log_flusher(LogFlusher* flusher) { flusher_ = flusher; }

  long hits() const { return hits_; }
  long misses() const { return misses_; }
  long evictions() const { return evictions_; }
  void print_stats() const;
  void reset_stats() { hits_ = misses_ = evictions_ = 0; }

 private:
  static constexpr uint8_t kMaxUsage = 5;

  int  find_frame(PageId id) const;  // frame holding id, or -1
  int  pick_victim();                // clock-sweep to a reusable frame
  void flush_frame(int frame);

  DiskManager& disk_;
  LogFlusher*  flusher_ = nullptr;

  std::vector<Page>               frames_;
  std::vector<uint8_t>            usage_;
  std::unordered_map<PageId, int> page_table_;  // page id -> frame index
  int  hand_ = 0;
  long hits_ = 0, misses_ = 0, evictions_ = 0;
};

}  // namespace minidb
