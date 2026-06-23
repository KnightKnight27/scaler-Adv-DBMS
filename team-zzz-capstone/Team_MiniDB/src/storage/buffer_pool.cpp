#include "buffer_pool.h"

#include <algorithm>

namespace minidb {

BufferPool::BufferPool(size_t pool_size, DiskManager *disk_manager)
    : pool_size_(pool_size), disk_manager_(disk_manager), pages_(pool_size) {
  for (size_t i = 0; i < pool_size_; i++) free_list_.push_back(i);
}

BufferPool::~BufferPool() { FlushAllPages(); }

bool BufferPool::FindFrameToEvict(size_t *frame_id) {
  if (!free_list_.empty()) {
    *frame_id = free_list_.front();
    free_list_.pop_front();
    return true;
  }
  // Evict the least-recently-used unpinned frame.
  for (auto it = lru_list_.rbegin(); it != lru_list_.rend(); ++it) {
    size_t fid = *it;
    if (pages_[fid].pin_count_ == 0) {
      lru_list_.erase(std::next(it).base());
      *frame_id = fid;
      return true;
    }
  }
  return false;  // pool is full of pinned pages
}

void BufferPool::TouchLru(size_t frame_id) {
  lru_list_.remove(frame_id);
  lru_list_.push_front(frame_id);
}

Page *BufferPool::FetchPage(page_id_t page_id) {
  std::lock_guard<std::mutex> guard(latch_);
  auto it = page_table_.find(page_id);
  if (it != page_table_.end()) {
    size_t fid = it->second;
    pages_[fid].pin_count_++;
    TouchLru(fid);
    return &pages_[fid];
  }
  size_t frame_id;
  if (!FindFrameToEvict(&frame_id)) return nullptr;

  Page &victim = pages_[frame_id];
  if (victim.page_id_ != INVALID_PAGE_ID) {
    if (victim.is_dirty_) disk_manager_->WritePage(victim.page_id_, victim.data_);
    page_table_.erase(victim.page_id_);
    lru_list_.remove(frame_id);
  }

  victim.ResetMemory();
  victim.page_id_ = page_id;
  victim.pin_count_ = 1;
  victim.is_dirty_ = false;
  disk_manager_->ReadPage(page_id, victim.data_);

  page_table_[page_id] = frame_id;
  TouchLru(frame_id);
  return &victim;
}

Page *BufferPool::NewPage(page_id_t *page_id) {
  std::lock_guard<std::mutex> guard(latch_);
  size_t frame_id;
  if (!FindFrameToEvict(&frame_id)) return nullptr;

  Page &victim = pages_[frame_id];
  if (victim.page_id_ != INVALID_PAGE_ID) {
    if (victim.is_dirty_) disk_manager_->WritePage(victim.page_id_, victim.data_);
    page_table_.erase(victim.page_id_);
    lru_list_.remove(frame_id);
  }

  page_id_t new_id = disk_manager_->AllocatePage();
  victim.ResetMemory();
  victim.page_id_ = new_id;
  victim.pin_count_ = 1;
  victim.is_dirty_ = true;

  page_table_[new_id] = frame_id;
  TouchLru(frame_id);
  *page_id = new_id;
  return &victim;
}

bool BufferPool::UnpinPage(page_id_t page_id, bool is_dirty) {
  std::lock_guard<std::mutex> guard(latch_);
  auto it = page_table_.find(page_id);
  if (it == page_table_.end()) return false;
  Page &p = pages_[it->second];
  if (p.pin_count_ <= 0) return false;
  p.pin_count_--;
  if (is_dirty) p.is_dirty_ = true;
  return true;
}

bool BufferPool::FlushPage(page_id_t page_id) {
  std::lock_guard<std::mutex> guard(latch_);
  auto it = page_table_.find(page_id);
  if (it == page_table_.end()) return false;
  Page &p = pages_[it->second];
  disk_manager_->WritePage(p.page_id_, p.data_);
  p.is_dirty_ = false;
  return true;
}

void BufferPool::FlushAllPages() {
  std::lock_guard<std::mutex> guard(latch_);
  for (auto &[pid, fid] : page_table_) {
    Page &p = pages_[fid];
    if (p.is_dirty_) {
      disk_manager_->WritePage(p.page_id_, p.data_);
      p.is_dirty_ = false;
    }
  }
}

bool BufferPool::DeletePage(page_id_t page_id) {
  std::lock_guard<std::mutex> guard(latch_);
  auto it = page_table_.find(page_id);
  if (it == page_table_.end()) return true;
  size_t fid = it->second;
  if (pages_[fid].pin_count_ > 0) return false;
  page_table_.erase(it);
  lru_list_.remove(fid);
  pages_[fid].ResetMemory();
  pages_[fid].page_id_ = INVALID_PAGE_ID;
  free_list_.push_back(fid);
  disk_manager_->DeallocatePage(page_id);
  return true;
}

}  // namespace minidb
