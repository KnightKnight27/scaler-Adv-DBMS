#pragma once
#include <list>
#include <unordered_map>
#include "common/config.h"

namespace minidb {

// Tracks which buffer-pool frames are eviction candidates and picks a victim in
// least-recently-used order. A frame is only a candidate while it is unpinned;
// the buffer pool calls Pin() when a frame becomes in-use and Unpin() when its
// pin count drops to zero.
class LRUReplacer {
 public:
  // Choose the least-recently-unpinned frame to evict. Returns false if there
  // are no eviction candidates.
  bool Victim(frame_id_t *out) {
    if (lru_list_.empty()) return false;
    *out = lru_list_.back();   // back = least recently used
    map_.erase(lru_list_.back());
    lru_list_.pop_back();
    return true;
  }

  // Remove a frame from the candidate set (it is now pinned / in use).
  void Pin(frame_id_t f) {
    auto it = map_.find(f);
    if (it == map_.end()) return;
    lru_list_.erase(it->second);
    map_.erase(it);
  }

  // Add a frame to the candidate set (just became unpinned). Most-recently-used.
  void Unpin(frame_id_t f) {
    if (map_.count(f)) return;  // already a candidate
    lru_list_.push_front(f);
    map_[f] = lru_list_.begin();
  }

  size_t Size() const { return lru_list_.size(); }

 private:
  std::list<frame_id_t> lru_list_;  // front = MRU, back = LRU
  std::unordered_map<frame_id_t, std::list<frame_id_t>::iterator> map_;
};

}  // namespace minidb
