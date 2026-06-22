#pragma once
#include <unordered_map>
#include <vector>
#include "common/config.h"
#include "storage/disk_manager.h"

namespace minidb {

// One in-memory page slot in the buffer pool.
struct Frame {
  char   data[PAGE_SIZE];
  PageId page_id;
  int    pin_count;  // number of active users; only unpinned frames are evictable
  bool   dirty;      // modified since loaded -> must be written before eviction
  bool   valid;      // holds a real page
  bool   ref;        // clock reference bit (second-chance)
  PageId page_lsn;   // log sequence number stamped on the page (WAL, used in M5)

  Frame()
      : page_id(INVALID_PAGE_ID), pin_count(0), dirty(false),
        valid(false), ref(false), page_lsn(0) {}
};

// A fixed-size pool of frames over a DiskManager, using the CLOCK
// (second-chance) replacement policy. Callers Fetch/New a page (which pins it),
// use frame->data, then Unpin it noting whether they dirtied it.
class BufferPool {
 public:
  BufferPool(DiskManager* dm, int num_frames);

  // Bring a page into memory (if needed) and pin it. Returns nullptr if every
  // frame is pinned and none can be evicted.
  Frame* FetchPage(PageId page_id);

  // Allocate a brand-new page on disk, bring it in zeroed, and pin it.
  Frame* NewPage(PageId* page_id_out);

  // Release a pin. If is_dirty, the frame is marked dirty.
  bool UnpinPage(PageId page_id, bool is_dirty);

  // Write a single dirty page back to disk (leaves it resident).
  bool FlushPage(PageId page_id);

  // Write all dirty pages back to disk.
  void FlushAll();

  // Observability for the storage demo / benchmarks.
  long Hits() const { return hits_; }
  long Misses() const { return misses_; }
  long Evictions() const { return evictions_; }
  void ResetStats() { hits_ = misses_ = evictions_ = 0; }

 private:
  int FindVictimFrame();  // CLOCK scan; returns frame index or -1 if all pinned

  DiskManager* dm_;
  std::vector<Frame> frames_;
  std::unordered_map<PageId, int> table_;  // page_id -> frame index
  int clock_hand_;

  long hits_;
  long misses_;
  long evictions_;
};

}  // namespace minidb
