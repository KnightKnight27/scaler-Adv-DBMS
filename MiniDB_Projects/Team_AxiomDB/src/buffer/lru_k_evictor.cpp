#include "buffer/lru_k_evictor.h"

#include <limits>

namespace axiomdb {

void LruKEvictor::record_access(frame_id_t fid) {
  FrameRecord& f = frames_[fid];
  f.in_use = true;
  f.history.push_back(current_ts_++);
  if (f.history.size() > k_) f.history.pop_front();
}

void LruKEvictor::set_evictable(frame_id_t fid, bool evictable) {
  FrameRecord& f = frames_[fid];
  if (!f.in_use) return;
  if (f.evictable == evictable) return;
  f.evictable = evictable;
  evictable_count_ += evictable ? 1 : -1;
}

std::optional<frame_id_t> LruKEvictor::evict() {
  // We want the largest backward-k-distance.  Frames with < k accesses have
  // infinite distance; we break ties among them (and they always win over
  // finite-distance frames) by smallest earliest-access timestamp = classic LRU.
  std::optional<frame_id_t> victim;
  bool victim_is_inf = false;
  size_t victim_kdist = 0;        // for finite case: larger is better
  size_t victim_earliest = std::numeric_limits<size_t>::max();  // for tie-break

  for (frame_id_t fid = 0; fid < static_cast<frame_id_t>(frames_.size()); ++fid) {
    const FrameRecord& f = frames_[fid];
    if (!f.in_use || !f.evictable) continue;

    bool is_inf = f.history.size() < k_;
    size_t earliest = f.history.front();
    // backward k-distance = now - (k-th most recent access) = now - history.front()
    // once history is capped at k entries.
    size_t kdist = is_inf ? 0 : (current_ts_ - f.history.front());

    bool better;
    if (!victim) {
      better = true;
    } else if (is_inf != victim_is_inf) {
      better = is_inf;                       // infinite distance always wins
    } else if (is_inf) {
      better = earliest < victim_earliest;   // both inf -> oldest first access
    } else {
      better = kdist > victim_kdist;         // both finite -> largest distance
    }

    if (better) {
      victim = fid;
      victim_is_inf = is_inf;
      victim_kdist = kdist;
      victim_earliest = earliest;
    }
  }

  if (victim) remove(*victim);
  return victim;
}

void LruKEvictor::remove(frame_id_t fid) {
  FrameRecord& f = frames_[fid];
  if (!f.in_use) return;
  if (f.evictable) --evictable_count_;
  f.history.clear();
  f.evictable = false;
  f.in_use = false;
}

}  // namespace axiomdb
