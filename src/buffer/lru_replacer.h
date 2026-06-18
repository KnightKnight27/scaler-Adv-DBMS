#pragma once

#include <list>
#include <mutex>
#include <unordered_map>

#include "buffer/replacer.h"

namespace minidb {

// Classic LRU eviction. Evictable frames live in a list ordered oldest -> newest
// (front = least-recently-used victim). A hash map gives O(1) removal so Pin /
// Unpin are constant time.
class LRUReplacer : public Replacer {
 public:
  explicit LRUReplacer(size_t num_frames);
  ~LRUReplacer() override = default;

  bool Victim(frame_id_t *frame_id) override;
  void Pin(frame_id_t frame_id) override;
  void Unpin(frame_id_t frame_id) override;
  size_t Size() override;

 private:
  std::mutex latch_;
  std::list<frame_id_t> lru_list_;  // front = LRU (next victim), back = most recent
  std::unordered_map<frame_id_t, std::list<frame_id_t>::iterator> table_;
  size_t capacity_;
};

}  // namespace minidb
