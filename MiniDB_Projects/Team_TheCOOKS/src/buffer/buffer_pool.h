#pragma once

#include <functional>
#include <list>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "buffer/lru_k_replacer.h"
#include "buffer/page.h"
#include "common/config.h"
#include "storage/disk_manager.h"

namespace walterdb {

// ---------------------------------------------------------------------------
// BufferPool -- caches a bounded set of disk pages in memory frames and
// mediates ALL page access for the layers above (heap file, B+tree).
//
// Core mechanics (the M1 milestone: "page manager + buffer pool integrated"):
//   * page table  : page_id -> frame_id
//   * pin counts  : a fetched page is pinned and cannot be evicted until every
//                   caller has unpinned it
//   * dirty flags : a modified page is written back to disk on eviction (the
//                   correct, current version -- never the stale one) and on
//                   flush
//   * LRU-K       : chooses which unpinned frame to evict under pressure
//
// A single latch protects the pool's metadata so heap/index code and (later)
// concurrent transactions can share it safely.  Logical concurrency control is
// the lock manager's job, not the pool's.
// ---------------------------------------------------------------------------

class BufferPool {
 public:
  BufferPool(DiskManager* disk, size_t pool_size = BUFFER_POOL_DEFAULT_FRAMES,
             size_t k = 2);

  BufferPool(const BufferPool&) = delete;
  BufferPool& operator=(const BufferPool&) = delete;

  // Allocate a brand-new page (grows the file) and return it pinned, or nullptr
  // if no frame is available.  *out_pid receives the new page id.
  Page* new_page(page_id_t* out_pid);

  // Fetch page `pid` into a frame and return it pinned, or nullptr if no frame
  // can be freed (all pinned).
  Page* fetch_page(page_id_t pid);

  // Release one pin on `pid`; mark dirty if the caller modified it.  Returns
  // false if the page is not resident or was not pinned.
  bool unpin_page(page_id_t pid, bool is_dirty);

  // Write `pid` back to disk if dirty (does not unpin).  Returns false if the
  // page is not resident.
  bool flush_page(page_id_t pid);

  // Flush every dirty resident page.
  void flush_all();

  DiskManager* disk() { return disk_; }
  size_t pool_size() const { return frames_.size(); }

  // Hook invoked just before ANY dirty page is written to disk.  The recovery
  // layer sets this to fsync the WAL, enforcing the write-ahead rule: a data
  // page can only reach disk after the log records describing it are durable.
  void set_pre_flush_hook(std::function<void()> hook) { pre_flush_ = std::move(hook); }

 private:
  // Find a frame to use: prefer the free list, else evict via LRU-K (flushing
  // the victim if dirty).  Returns INVALID_FRAME_ID if everything is pinned.
  // Caller must hold latch_.
  frame_id_t grab_frame();

  DiskManager* disk_;
  std::vector<Page> frames_;
  std::unordered_map<page_id_t, frame_id_t> page_table_;
  std::list<frame_id_t> free_list_;
  LRUKReplacer replacer_;
  std::function<void()> pre_flush_;
  std::mutex latch_;
};

}  // namespace walterdb
