#include "storage/buffer_pool.h"

#include "recovery/log_manager.h"

namespace minidb {

// ----------------------------- LRUReplacer ---------------------------------
bool LRUReplacer::victim(int *frame_id) {
  if (list_.empty()) return false;
  *frame_id = list_.back();   // least recently used
  pos_.erase(list_.back());
  list_.pop_back();
  return true;
}

void LRUReplacer::pin(int frame_id) {
  auto it = pos_.find(frame_id);
  if (it == pos_.end()) return;  // already pinned / not tracked
  list_.erase(it->second);
  pos_.erase(it);
}

void LRUReplacer::unpin(int frame_id) {
  if (pos_.count(frame_id)) return;     // already evictable
  list_.push_front(frame_id);           // most recently used
  pos_[frame_id] = list_.begin();
}

// ----------------------------- BufferPool ----------------------------------
BufferPool::BufferPool(DiskManager *dm, int pool_size)
    : disk_manager_(dm), pool_size_(pool_size), frames_(pool_size) {
  for (int i = 0; i < pool_size_; ++i) free_list_.push_back(i);
}

int BufferPool::grabFrame() {
  if (!free_list_.empty()) {
    int f = free_list_.front();
    free_list_.pop_front();
    return f;
  }
  int victim;
  if (!replacer_.victim(&victim)) return -1;  // everything is pinned

  Page &p = frames_[victim];
  if (p.is_dirty_) {                          // write-back before reuse
    if (log_manager_) log_manager_->flush();  // WAL rule: log before data page
    disk_manager_->writePage(p.page_id_, p.data_);
    p.is_dirty_ = false;
  }
  page_table_.erase(p.page_id_);              // old occupant is gone
  return victim;
}

Page *BufferPool::fetchPage(page_id_t page_id) {
  lock_guard<mutex> g(latch_);

  // Case 1: already cached -> pin and return.
  auto it = page_table_.find(page_id);
  if (it != page_table_.end()) {
    int f = it->second;
    frames_[f].pin_count_++;
    replacer_.pin(f);
    return &frames_[f];
  }

  // Case 2: not cached -> grab a frame and load from disk.
  int f = grabFrame();
  if (f == -1) return nullptr;

  Page &p = frames_[f];
  disk_manager_->readPage(page_id, p.data_);
  p.page_id_ = page_id;
  p.pin_count_ = 1;
  p.is_dirty_ = false;
  page_table_[page_id] = f;
  replacer_.pin(f);
  return &p;
}

Page *BufferPool::newPage(page_id_t *page_id) {
  lock_guard<mutex> g(latch_);

  int f = grabFrame();
  if (f == -1) return nullptr;

  page_id_t new_id = disk_manager_->allocatePage();
  Page &p = frames_[f];
  p.reset();                 // zero out: a new page starts empty
  p.page_id_ = new_id;
  p.pin_count_ = 1;
  p.is_dirty_ = true;        // must be written at least once
  page_table_[new_id] = f;
  replacer_.pin(f);
  *page_id = new_id;
  return &p;
}

bool BufferPool::unpinPage(page_id_t page_id, bool is_dirty) {
  lock_guard<mutex> g(latch_);
  auto it = page_table_.find(page_id);
  if (it == page_table_.end()) return false;

  int f = it->second;
  Page &p = frames_[f];
  if (is_dirty) p.is_dirty_ = true;   // sticky: stays dirty until flushed
  if (p.pin_count_ <= 0) return false;

  if (--p.pin_count_ == 0) replacer_.unpin(f);  // now evictable
  return true;
}

bool BufferPool::flushPage(page_id_t page_id) {
  lock_guard<mutex> g(latch_);
  auto it = page_table_.find(page_id);
  if (it == page_table_.end()) return false;
  Page &p = frames_[it->second];
  if (log_manager_) log_manager_->flush();   // WAL rule
  disk_manager_->writePage(page_id, p.data_);
  p.is_dirty_ = false;
  return true;
}

void BufferPool::flushAll() {
  lock_guard<mutex> g(latch_);
  if (log_manager_) log_manager_->flush();    // WAL rule
  for (auto &[pid, f] : page_table_) {
    Page &p = frames_[f];
    if (p.is_dirty_) {
      disk_manager_->writePage(pid, p.data_);
      p.is_dirty_ = false;
    }
  }
}

}  // namespace minidb
