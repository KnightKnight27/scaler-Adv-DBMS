#include "storage/buffer_pool_manager.h"

namespace minidb {

BufferPoolManager::BufferPoolManager(DiskManager *disk, size_t pool_size)
    : disk_(disk), pool_size_(pool_size), frames_(pool_size) {
  // Every frame starts free.
  for (frame_id_t f = 0; f < static_cast<frame_id_t>(pool_size_); ++f) {
    free_list_.push_back(f);
  }
}

bool BufferPoolManager::GetVictimFrame(frame_id_t *frame) {
  if (!free_list_.empty()) {
    *frame = free_list_.back();
    free_list_.pop_back();
    return true;
  }
  frame_id_t victim;
  if (!replacer_.Victim(&victim)) return false;  // everything is pinned
  Page &v = frames_[victim];
  if (v.is_dirty_) {
    disk_->WritePage(v.page_id_, v.data_);  // flush before reuse
  }
  page_table_.erase(v.page_id_);
  *frame = victim;
  return true;
}

Page *BufferPoolManager::FetchPage(page_id_t page_id) {
  // Already resident: pin and return.
  auto it = page_table_.find(page_id);
  if (it != page_table_.end()) {
    frame_id_t f = it->second;
    frames_[f].pin_count_++;
    replacer_.Pin(f);
    return &frames_[f];
  }
  // Not resident: find a frame, load from disk.
  frame_id_t f;
  if (!GetVictimFrame(&f)) return nullptr;
  Page &p = frames_[f];
  p.Reset();
  p.page_id_ = page_id;
  p.pin_count_ = 1;
  p.is_dirty_ = false;
  disk_->ReadPage(page_id, p.data_);
  page_table_[page_id] = f;
  replacer_.Pin(f);
  return &p;
}

Page *BufferPoolManager::NewPage(page_id_t *page_id) {
  frame_id_t f;
  if (!GetVictimFrame(&f)) return nullptr;
  page_id_t new_id = disk_->AllocatePage();
  Page &p = frames_[f];
  p.Reset();
  p.page_id_ = new_id;
  p.pin_count_ = 1;
  p.is_dirty_ = true;  // a fresh page must be written out eventually
  page_table_[new_id] = f;
  replacer_.Pin(f);
  *page_id = new_id;
  return &p;
}

bool BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty) {
  auto it = page_table_.find(page_id);
  if (it == page_table_.end()) return false;
  frame_id_t f = it->second;
  Page &p = frames_[f];
  if (p.pin_count_ <= 0) return false;
  p.pin_count_--;
  if (is_dirty) p.is_dirty_ = true;
  if (p.pin_count_ == 0) replacer_.Unpin(f);  // now an eviction candidate
  return true;
}

bool BufferPoolManager::FlushPage(page_id_t page_id) {
  auto it = page_table_.find(page_id);
  if (it == page_table_.end()) return false;
  Page &p = frames_[it->second];
  disk_->WritePage(p.page_id_, p.data_);
  p.is_dirty_ = false;
  return true;
}

void BufferPoolManager::FlushAll() {
  for (auto &kv : page_table_) {
    Page &p = frames_[kv.second];
    if (p.is_dirty_) {
      disk_->WritePage(p.page_id_, p.data_);
      p.is_dirty_ = false;
    }
  }
  disk_->Flush();
}

}  // namespace minidb
