// ============================================================================
// buffer_pool.h  --  Caches a fixed number of disk pages in memory.
//
// The buffer pool is the middle layer between "the file on disk" (DiskManager)
// and "code that wants to read/write a tuple".  Because RAM is far smaller than
// the database, only BUFFER_POOL_SIZE pages fit at once.  When all frames are
// full and a new page is needed, the LRU replacer chooses a victim to evict.
//
// The two rules that keep everything correct:
//   1. PIN / UNPIN.  A caller that holds a Page* must keep it pinned so it is
//      not evicted from under them.  Forgetting to unpin leaks a frame.
//   2. STEAL + dirty write-back.  A dirty victim is written back to disk before
//      its frame is reused, so modifications are never lost on eviction.
//
// LRU = "Least Recently Used": the page untouched for the longest time is the
// first evicted, on the bet that it is least likely to be needed soon.
// ============================================================================
#pragma once

#include "common/common.h"
#include "storage/disk_manager.h"
#include "storage/page.h"

namespace minidb {

class LogManager;  // forward declaration; storage stays decoupled from recovery

// --- LRU replacer: tracks which *unpinned* frames may be evicted, in order. ---
class LRUReplacer {
 public:
  // Pick the least-recently-used frame as victim.  Returns false if none.
  bool victim(int *frame_id);
  // The frame is now in use (pinned) -> not a candidate for eviction.
  void pin(int frame_id);
  // The frame is now free of users -> becomes the most-recently-used candidate.
  void unpin(int frame_id);
  size_t size() const { return list_.size(); }

 private:
  // front = most recently used, back = least recently used (the victim).
  list<int> list_;
  unordered_map<int, list<int>::iterator> pos_;
};

class BufferPool {
 public:
  BufferPool(DiskManager *dm, int pool_size = BUFFER_POOL_SIZE);

  // Bring page `page_id` into a frame and pin it.  Returns nullptr if every
  // frame is pinned (the pool is exhausted).  Caller MUST unpinPage when done.
  Page *fetchPage(page_id_t page_id);

  // Allocate a brand-new page on disk, bring it into a pinned frame, and return
  // it.  The new page id is written to *page_id.
  Page *newPage(page_id_t *page_id);

  // Release a pin.  Pass is_dirty=true if you modified the page's bytes.
  bool unpinPage(page_id_t page_id, bool is_dirty);

  // Write one page (or all dirty pages) back to disk immediately.
  bool flushPage(page_id_t page_id);
  void flushAll();

  DiskManager *disk_manager() { return disk_manager_; }

  // Optional: once set, the pool flushes the WAL before writing any dirty page,
  // upholding the write-ahead rule.  Wired up at database startup.
  void setLogManager(LogManager *lm) { log_manager_ = lm; }

 private:
  // Find a frame to use: prefer the free list, else evict an LRU victim.
  // Returns -1 if no frame is available.  Flushes the victim if it was dirty.
  int grabFrame();

  DiskManager      *disk_manager_;
  int               pool_size_;
  vector<Page> frames_;                          // the actual frames
  unordered_map<page_id_t, int> page_table_;     // page_id -> frame index
  list<int>    free_list_;                        // unused frame indices
  LRUReplacer       replacer_;
  LogManager       *log_manager_{nullptr};             // optional WAL hook
  mutex        latch_;                            // protects all the above
};

}  // namespace minidb
