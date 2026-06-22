#pragma once
#include <list>
#include <unordered_map>
#include <mutex>
#include "common/config.h"

namespace minidb {

// LRU replacement policy over frames that are currently *unpinned* and thus
// eviction candidates. A frame is added here when its pin count hits 0 and
// removed when it is pinned again (or chosen as a victim).
class LRUReplacer {
 public:
  // Pick the least-recently-used frame as a victim. Returns false if empty.
  bool Victim(frame_id_t* out) {
    std::lock_guard<std::mutex> lk(latch_);
    if (lru_list_.empty()) return false;
    frame_id_t victim = lru_list_.back();  // back = least recently used
    lru_list_.pop_back();
    table_.erase(victim);
    *out = victim;
    return true;
  }

  // Remove a frame from the replacer (called when it gets pinned).
  void Pin(frame_id_t frame_id) {
    std::lock_guard<std::mutex> lk(latch_);
    auto it = table_.find(frame_id);
    if (it == table_.end()) return;
    lru_list_.erase(it->second);
    table_.erase(it);
  }

  // Mark a frame as an eviction candidate (pin count reached 0).
  void Unpin(frame_id_t frame_id) {
    std::lock_guard<std::mutex> lk(latch_);
    if (table_.count(frame_id)) return;  // already a candidate
    lru_list_.push_front(frame_id);      // front = most recently used
    table_[frame_id] = lru_list_.begin();
  }

  size_t Size() {
    std::lock_guard<std::mutex> lk(latch_);
    return lru_list_.size();
  }

 private:
  std::list<frame_id_t> lru_list_;  // front = MRU, back = LRU
  std::unordered_map<frame_id_t, std::list<frame_id_t>::iterator> table_;
  std::mutex latch_;
};

}  // namespace minidb
