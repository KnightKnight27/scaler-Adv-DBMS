#include "storage/buffer_pool.h"
#include <iostream>
#include <stdexcept>

namespace minidb {

BufferPool::BufferPool(DiskManager& disk) : disk_(disk) {
  frames_.resize(BUFFER_FRAMES);
  usage_.assign(BUFFER_FRAMES, 0);
}

int BufferPool::find_frame(PageId id) const {
  auto it = page_table_.find(id);
  return it == page_table_.end() ? -1 : it->second;
}

Page* BufferPool::fetch_page(PageId id) {
  int frame = find_frame(id);
  if (frame >= 0) {  // cache hit
    ++hits_;
    frames_[frame].pin_count += 1;
    if (usage_[frame] < kMaxUsage) usage_[frame] += 1;
    return &frames_[frame];
  }

  ++misses_;  // cache miss: find a frame and load from disk
  frame = pick_victim();
  Page& f = frames_[frame];
  disk_.read_page(id, f);
  f.dirty     = false;
  f.pin_count = 1;
  usage_[frame]       = 1;
  page_table_[id]     = frame;
  return &f;
}

void BufferPool::unpin_page(PageId id, bool dirty) {
  int frame = find_frame(id);
  if (frame < 0) return;
  if (dirty) frames_[frame].dirty = true;
  if (frames_[frame].pin_count > 0) frames_[frame].pin_count -= 1;
}

PageId BufferPool::allocate_page() { return disk_.allocate_page(); }

int BufferPool::pick_victim() {
  // Prefer an empty frame so the cache fills before any eviction happens.
  for (int i = 0; i < static_cast<int>(frames_.size()); ++i) {
    if (frames_[i].id == kInvalidPage) return i;
  }
  // If every frame is pinned there is no victim to take.
  bool any_unpinned = false;
  for (const Page& f : frames_) if (f.pin_count == 0) { any_unpinned = true; break; }
  if (!any_unpinned) throw std::runtime_error("BufferPool: all frames pinned, cannot evict");

  // Clock sweep: skip pinned frames, decay usage; an unpinned frame's usage
  // drops to 0 within a bounded number of passes, so this always terminates.
  while (true) {
    int i = hand_;
    hand_ = (hand_ + 1) % static_cast<int>(frames_.size());
    if (frames_[i].pin_count > 0) continue;
    if (usage_[i] > 0) { usage_[i] -= 1; continue; }
    flush_frame(i);
    page_table_.erase(frames_[i].id);
    ++evictions_;
    return i;
  }
}

void BufferPool::flush_frame(int frame) {
  Page& f = frames_[frame];
  if (f.id == kInvalidPage || !f.dirty) return;
  // Write-ahead rule: log describing this page must be durable before the page.
  const PageHeader* h = reinterpret_cast<const PageHeader*>(f.data.data());
  if (flusher_ && h->rec_lsn != kInvalidLSN) flusher_->flush_upto(h->rec_lsn);
  disk_.write_page(f.id, f);
  f.dirty = false;
}

void BufferPool::flush_page(PageId id) {
  int frame = find_frame(id);
  if (frame >= 0) flush_frame(frame);
}

void BufferPool::flush_all() {
  for (int i = 0; i < static_cast<int>(frames_.size()); ++i) flush_frame(i);
}

void BufferPool::print_stats() const {
  std::cout << "[buffer pool] hits=" << hits_ << " misses=" << misses_
            << " evictions=" << evictions_ << "\n";
}

}  // namespace minidb
