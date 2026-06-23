#include "minidb/storage/buffer_pool.hpp"

#include <stdexcept>

namespace minidb {

BufferPool::BufferPool(PageManager& pm, size_t num_frames) : pm_(pm) {
  if (num_frames == 0) num_frames = 1;
  frames_.resize(num_frames);
  free_.reserve(num_frames);
  for (size_t i = 0; i < num_frames; ++i) free_.push_back(i);
}

void BufferPool::lru_remove(size_t idx) {
  auto it = lru_pos_.find(idx);
  if (it != lru_pos_.end()) {
    lru_.erase(it->second);
    lru_pos_.erase(it);
  }
}

void BufferPool::lru_add(size_t idx) {
  lru_.push_back(idx);  // most-recently-used at the back
  lru_pos_[idx] = std::prev(lru_.end());
}

size_t BufferPool::acquire_frame() {
  if (!free_.empty()) {
    size_t idx = free_.back();
    free_.pop_back();
    return idx;
  }
  if (lru_.empty()) throw std::runtime_error("BufferPool: all pages pinned");
  size_t idx = lru_.front();
  lru_.pop_front();
  lru_pos_.erase(idx);
  Frame& f = frames_[idx];
  if (f.dirty) {
    pm_.write_page(f.page_id, f.data.data());
    f.dirty = false;
  }
  table_.erase(f.page_id);
  return idx;
}

uint8_t* BufferPool::fetch_page(PageId id) {
  auto it = table_.find(id);
  if (it != table_.end()) {
    ++hits_;
    size_t idx = it->second;
    if (frames_[idx].pin == 0) lru_remove(idx);
    ++frames_[idx].pin;
    return frames_[idx].data.data();
  }
  ++misses_;
  size_t idx = acquire_frame();
  Frame& f = frames_[idx];
  pm_.read_page(id, f.data.data());
  f.page_id = id;
  f.pin = 1;
  f.dirty = false;
  table_[id] = idx;
  return f.data.data();
}

PageId BufferPool::new_page(uint8_t*& out) {
  PageId id = pm_.allocate_page();
  size_t idx = acquire_frame();
  Frame& f = frames_[idx];
  pm_.read_page(id, f.data.data());  // zeroed page
  f.page_id = id;
  f.pin = 1;
  f.dirty = true;
  table_[id] = idx;
  out = f.data.data();
  return id;
}

void BufferPool::unpin(PageId id, bool dirty) {
  auto it = table_.find(id);
  if (it == table_.end()) return;
  size_t idx = it->second;
  Frame& f = frames_[idx];
  if (dirty) f.dirty = true;
  if (f.pin > 0) --f.pin;
  if (f.pin == 0) lru_add(idx);
}

void BufferPool::flush_all() {
  for (auto& f : frames_) {
    if (f.page_id != kInvalidPageId && f.dirty) {
      pm_.write_page(f.page_id, f.data.data());
      f.dirty = false;
    }
  }
  pm_.sync();
}

}  // namespace minidb
