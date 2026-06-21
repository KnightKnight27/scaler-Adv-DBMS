#include "buffer/buffer_pool.h"

namespace walterdb {

BufferPool::BufferPool(DiskManager* disk, size_t pool_size, size_t k)
    : disk_(disk), frames_(pool_size), replacer_(pool_size, k) {
  for (frame_id_t i = 0; i < static_cast<frame_id_t>(pool_size); ++i) {
    free_list_.push_back(i);
  }
}

frame_id_t BufferPool::grab_frame() {
  if (!free_list_.empty()) {
    frame_id_t fid = free_list_.front();
    free_list_.pop_front();
    return fid;
  }
  auto victim = replacer_.evict();
  if (!victim) return INVALID_FRAME_ID;
  frame_id_t fid = *victim;
  Page& f = frames_[fid];
  if (f.is_dirty_) {
    // Write the *current* contents back before reusing the frame, so the page
    // we are about to drop is not silently rolled back to its stale on-disk
    // version.  Honour write-ahead first: make the log durable before the page.
    if (pre_flush_) pre_flush_();
    disk_->write_page(f.page_id_, f.data_);
  }
  page_table_.erase(f.page_id_);
  return fid;
}

Page* BufferPool::new_page(page_id_t* out_pid) {
  std::lock_guard<std::mutex> guard(latch_);
  frame_id_t fid = grab_frame();
  if (fid == INVALID_FRAME_ID) return nullptr;

  page_id_t pid = disk_->allocate_page();
  Page& f = frames_[fid];
  f.reset(pid);
  f.pin_count_ = 1;
  // A freshly created page only holds the caller's content once they write it,
  // so it must be flushed: mark dirty up front.
  f.is_dirty_ = true;

  page_table_[pid] = fid;
  replacer_.record_access(fid);
  replacer_.set_evictable(fid, false);

  if (out_pid) *out_pid = pid;
  return &f;
}

Page* BufferPool::fetch_page(page_id_t pid) {
  std::lock_guard<std::mutex> guard(latch_);

  auto it = page_table_.find(pid);
  if (it != page_table_.end()) {
    frame_id_t fid = it->second;
    Page& f = frames_[fid];
    ++f.pin_count_;
    replacer_.record_access(fid);
    replacer_.set_evictable(fid, false);
    return &f;
  }

  frame_id_t fid = grab_frame();
  if (fid == INVALID_FRAME_ID) return nullptr;

  Page& f = frames_[fid];
  f.reset(pid);
  if (!disk_->read_page(pid, f.data_).ok()) {
    // Could not read: return the frame to the free list and fail.
    free_list_.push_back(fid);
    return nullptr;
  }
  f.pin_count_ = 1;
  f.is_dirty_ = false;

  page_table_[pid] = fid;
  replacer_.record_access(fid);
  replacer_.set_evictable(fid, false);
  return &f;
}

bool BufferPool::unpin_page(page_id_t pid, bool is_dirty) {
  std::lock_guard<std::mutex> guard(latch_);
  auto it = page_table_.find(pid);
  if (it == page_table_.end()) return false;
  frame_id_t fid = it->second;
  Page& f = frames_[fid];
  if (f.pin_count_ <= 0) return false;
  if (is_dirty) f.is_dirty_ = true;
  if (--f.pin_count_ == 0) {
    replacer_.set_evictable(fid, true);
  }
  return true;
}

bool BufferPool::flush_page(page_id_t pid) {
  std::lock_guard<std::mutex> guard(latch_);
  auto it = page_table_.find(pid);
  if (it == page_table_.end()) return false;
  Page& f = frames_[it->second];
  if (f.is_dirty_) {
    if (pre_flush_) pre_flush_();
    disk_->write_page(f.page_id_, f.data_);
    f.is_dirty_ = false;
  }
  return true;
}

void BufferPool::flush_all() {
  std::lock_guard<std::mutex> guard(latch_);
  bool synced = false;
  for (Page& f : frames_) {
    if (f.page_id_ != INVALID_PAGE_ID && f.is_dirty_) {
      if (pre_flush_ && !synced) { pre_flush_(); synced = true; }  // one log sync covers the batch
      disk_->write_page(f.page_id_, f.data_);
      f.is_dirty_ = false;
    }
  }
}

}  // namespace walterdb
