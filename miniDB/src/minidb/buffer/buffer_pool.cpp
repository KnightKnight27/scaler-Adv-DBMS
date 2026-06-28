#include "minidb/buffer/buffer_pool.h"

#include <algorithm>

namespace minidb {

BufferPool::BufferPool(DiskManager& disk, std::size_t capacity)
    : disk_(disk), capacity_(std::max<std::size_t>(1, capacity)) {}

BufferPool::~BufferPool() { FlushAll(); }

Page& BufferPool::NewPage() {
  EvictIfNeeded();
  PageId page_id = disk_.AllocatePage();
  auto frame = std::make_unique<Frame>();
  disk_.ReadPage(page_id, frame->page);
  frame->pin_count = 1;
  auto& page = frame->page;
  frames_[page_id] = std::move(frame);
  Touch(page_id);
  return page;
}

Page& BufferPool::FetchPage(PageId page_id) {
  auto it = frames_.find(page_id);
  if (it == frames_.end()) {
    EvictIfNeeded();
    auto frame = std::make_unique<Frame>();
    disk_.ReadPage(page_id, frame->page);
    it = frames_.emplace(page_id, std::move(frame)).first;
  }
  it->second->pin_count++;
  Touch(page_id);
  return it->second->page;
}

void BufferPool::UnpinPage(PageId page_id, bool dirty) {
  auto it = frames_.find(page_id);
  if (it == frames_.end()) {
    return;
  }
  if (it->second->pin_count > 0) {
    it->second->pin_count--;
  }
  it->second->dirty = it->second->dirty || dirty;
  Touch(page_id);
}

void BufferPool::FlushPage(PageId page_id) {
  auto it = frames_.find(page_id);
  if (it == frames_.end()) {
    return;
  }
  disk_.WritePage(it->second->page);
  it->second->dirty = false;
}

void BufferPool::FlushAll() {
  for (auto& [page_id, frame] : frames_) {
    if (frame->dirty) {
      disk_.WritePage(frame->page);
      frame->dirty = false;
    }
  }
  disk_.Flush();
}

void BufferPool::Touch(PageId page_id) {
  lru_.remove(page_id);
  lru_.push_front(page_id);
}

void BufferPool::EvictIfNeeded() {
  if (frames_.size() < capacity_) {
    return;
  }
  for (auto it = lru_.rbegin(); it != lru_.rend(); ++it) {
    auto frame_it = frames_.find(*it);
    if (frame_it != frames_.end() && frame_it->second->pin_count == 0) {
      PageId victim = *it;
      if (frame_it->second->dirty) {
        disk_.WritePage(frame_it->second->page);
      }
      frames_.erase(frame_it);
      lru_.remove(victim);
      return;
    }
  }
  throw MiniDbError("buffer pool has no unpinned frame to evict");
}

}  // namespace minidb
