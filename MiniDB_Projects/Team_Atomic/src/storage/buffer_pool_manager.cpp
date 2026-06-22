#include "storage/buffer_pool_manager.h"
#include "common/types.h"

namespace minidb {

BufferPoolManager::BufferPoolManager(size_t pool_size, DiskManager* disk_manager)
    : pool_size_(pool_size), disk_manager_(disk_manager), frames_(pool_size) {
  for (size_t i = 0; i < pool_size_; i++) {
    free_list_.push_back(static_cast<frame_id_t>(i));
  }
}

BufferPoolManager::~BufferPoolManager() { FlushAll(); }

frame_id_t BufferPoolManager::GetFreeFrame() {
  // Prefer a never-used frame.
  if (!free_list_.empty()) {
    frame_id_t fid = free_list_.front();
    free_list_.pop_front();
    return fid;
  }
  // Otherwise evict an LRU victim.
  frame_id_t victim;
  if (!replacer_.Victim(&victim)) return -1;  // everything is pinned
  Page& v = frames_[victim];
  if (v.is_dirty_) {
    disk_manager_->WritePage(v.page_id_, v.data_);
  }
  page_table_.erase(v.page_id_);
  v.Reset();
  return victim;
}

Page* BufferPoolManager::FetchPage(page_id_t page_id) {
  std::lock_guard<std::mutex> lk(latch_);
  // Already cached?
  auto it = page_table_.find(page_id);
  if (it != page_table_.end()) {
    frame_id_t fid = it->second;
    Page& p = frames_[fid];
    p.pin_count_++;
    replacer_.Pin(fid);
    return &p;
  }
  // Need to load from disk.
  frame_id_t fid = GetFreeFrame();
  if (fid == -1) return nullptr;
  Page& p = frames_[fid];
  p.page_id_ = page_id;
  p.pin_count_ = 1;
  p.is_dirty_ = false;
  disk_manager_->ReadPage(page_id, p.data_);
  page_table_[page_id] = fid;
  replacer_.Pin(fid);
  return &p;
}

Page* BufferPoolManager::NewPage(page_id_t* page_id) {
  std::lock_guard<std::mutex> lk(latch_);
  frame_id_t fid = GetFreeFrame();
  if (fid == -1) return nullptr;
  page_id_t new_id = disk_manager_->AllocatePage();
  Page& p = frames_[fid];
  p.Reset();
  p.page_id_ = new_id;
  p.pin_count_ = 1;
  p.is_dirty_ = true;  // a fresh page is dirty until flushed
  page_table_[new_id] = fid;
  replacer_.Pin(fid);
  *page_id = new_id;
  return &p;
}

bool BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty) {
  std::lock_guard<std::mutex> lk(latch_);
  auto it = page_table_.find(page_id);
  if (it == page_table_.end()) return false;
  frame_id_t fid = it->second;
  Page& p = frames_[fid];
  if (p.pin_count_ <= 0) return false;
  if (is_dirty) p.is_dirty_ = true;
  p.pin_count_--;
  if (p.pin_count_ == 0) replacer_.Unpin(fid);
  return true;
}

bool BufferPoolManager::FlushPage(page_id_t page_id) {
  std::lock_guard<std::mutex> lk(latch_);
  auto it = page_table_.find(page_id);
  if (it == page_table_.end()) return false;
  Page& p = frames_[it->second];
  disk_manager_->WritePage(p.page_id_, p.data_);
  p.is_dirty_ = false;
  return true;
}

void BufferPoolManager::FlushAll() {
  std::lock_guard<std::mutex> lk(latch_);
  for (auto& [pid, fid] : page_table_) {
    Page& p = frames_[fid];
    if (p.is_dirty_) {
      disk_manager_->WritePage(p.page_id_, p.data_);
      p.is_dirty_ = false;
    }
  }
}

}  // namespace minidb
