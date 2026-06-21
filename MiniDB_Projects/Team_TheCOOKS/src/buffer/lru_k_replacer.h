#pragma once

#include <cstddef>
#include <list>
#include <optional>
#include <vector>

#include "common/config.h"

namespace walterdb {

// ---------------------------------------------------------------------------
// LRUKReplacer -- the buffer pool's page-replacement policy.
//
// LRU-K ( 2 here) evicts the frame whose K-th most recent access is furthest in
// the past ("largest backward K-distance").  Compared to plain LRU it resists
// sequential-scan pollution: a page touched once by a one-shot scan has fewer
// than K accesses and is treated as having infinite distance, so it is evicted
// before a page that is genuinely hot (accessed K+ times recently).
//
//   * frames with < K recorded accesses  -> distance = +inf; among these we
//     fall back to classic LRU (evict the earliest first access).
//   * otherwise distance = now - timestamp_of_kth_previous_access.
//
// Only frames marked evictable (pin_count == 0) are candidates.  This class is
// not internally synchronised; the BufferPool calls it under its own latch.
// ---------------------------------------------------------------------------

class LRUKReplacer {
 public:
  LRUKReplacer(size_t num_frames, size_t k)
      : k_(k), frames_(num_frames) {}

  // Record that `fid` was just accessed (fetch / new / pin).
  void record_access(frame_id_t fid);

  // Mark whether `fid` may be chosen as a victim (true once pin_count hits 0).
  void set_evictable(frame_id_t fid, bool evictable);

  // Choose and remove a victim frame, or nullopt if nothing is evictable.
  std::optional<frame_id_t> evict();

  // Forget a frame entirely (e.g. its page was removed from the pool).
  void remove(frame_id_t fid);

  size_t evictable_count() const { return evictable_count_; }

 private:
  struct FrameRecord {
    std::list<size_t> history;  // access timestamps, most recent at back, capped at k
    bool evictable = false;
    bool in_use = false;        // has this frame ever been recorded?
  };

  size_t k_;
  size_t current_ts_ = 0;
  size_t evictable_count_ = 0;
  std::vector<FrameRecord> frames_;
};

}  // namespace walterdb
