#include "buffer/lru_replacer.h"

namespace minidb {

LRUReplacer::LRUReplacer(size_t num_frames) : capacity_(num_frames) {}

bool LRUReplacer::Victim(frame_id_t *frame_id) {
  std::lock_guard<std::mutex> g(latch_);
  if (lru_list_.empty()) return false;
  *frame_id = lru_list_.front();  // least recently used
  lru_list_.pop_front();
  table_.erase(*frame_id);
  return true;
}

void LRUReplacer::Pin(frame_id_t frame_id) {
  std::lock_guard<std::mutex> g(latch_);
  auto it = table_.find(frame_id);
  if (it != table_.end()) {
    lru_list_.erase(it->second);
    table_.erase(it);
  }
}

void LRUReplacer::Unpin(frame_id_t frame_id) {
  std::lock_guard<std::mutex> g(latch_);
  if (table_.find(frame_id) != table_.end()) return;  // already evictable
  if (table_.size() >= capacity_) return;             // defensive: never exceed pool
  lru_list_.push_back(frame_id);                       // newest at back
  table_[frame_id] = std::prev(lru_list_.end());
}

size_t LRUReplacer::Size() {
  std::lock_guard<std::mutex> g(latch_);
  return lru_list_.size();
}

}  // namespace minidb
