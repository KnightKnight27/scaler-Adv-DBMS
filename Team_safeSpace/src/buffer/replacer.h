#pragma once

#include <cstddef>

#include "common/config.h"

namespace minidb {

// Eviction-policy interface. The buffer pool asks the replacer for a victim
// frame when no free frame is available. Keeping this abstract lets the policy
// (LRU here) be swapped without touching the buffer pool.
class Replacer {
 public:
  virtual ~Replacer() = default;

  // Pick a frame to evict; return false if there is nothing evictable.
  virtual bool Victim(frame_id_t *frame_id) = 0;
  // Mark a frame as in-use (not evictable) — called when a page is pinned.
  virtual void Pin(frame_id_t frame_id) = 0;
  // Mark a frame as evictable — called when a page's pin count hits zero.
  virtual void Unpin(frame_id_t frame_id) = 0;
  // Number of frames currently eligible for eviction.
  virtual size_t Size() = 0;
};

}  // namespace minidb
