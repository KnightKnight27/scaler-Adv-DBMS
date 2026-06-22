#include "storage/buffer_pool.h"

#include <cstring>

namespace minidb {

BufferPool::BufferPool(DiskManager* dm, int num_frames)
    : dm_(dm), frames_(num_frames), clock_hand_(0),
      hits_(0), misses_(0), evictions_(0) {}

int BufferPool::FindVictimFrame() {
  const int n = static_cast<int>(frames_.size());
  // CLOCK / second-chance: sweep at most twice so every frame's ref bit gets a
  // chance to be cleared before we give up.
  for (int scanned = 0; scanned < 2 * n; ++scanned) {
    Frame& f = frames_[clock_hand_];
    int idx = clock_hand_;
    clock_hand_ = (clock_hand_ + 1) % n;

    if (!f.valid) return idx;            // empty slot -- use it
    if (f.pin_count > 0) continue;       // in use -- cannot evict
    if (f.ref) { f.ref = false; continue; }  // give second chance
    return idx;                          // unpinned, ref already clear -> victim
  }
  return -1;  // everything is pinned
}

Frame* BufferPool::FetchPage(PageId page_id) {
  auto it = table_.find(page_id);
  if (it != table_.end()) {
    Frame& f = frames_[it->second];
    f.pin_count++;
    f.ref = true;
    hits_++;
    return &f;
  }

  misses_++;
  int idx = FindVictimFrame();
  if (idx < 0) return nullptr;

  Frame& f = frames_[idx];
  if (f.valid) {
    if (f.dirty) {
      dm_->WritePage(f.page_id, f.data);
      f.dirty = false;
    }
    table_.erase(f.page_id);
    evictions_++;
  }

  dm_->ReadPage(page_id, f.data);
  f.page_id = page_id;
  f.pin_count = 1;
  f.dirty = false;
  f.valid = true;
  f.ref = true;
  table_[page_id] = idx;
  return &f;
}

Frame* BufferPool::NewPage(PageId* page_id_out) {
  PageId pid = dm_->AllocatePage();
  Frame* f = FetchPage(pid);  // reads back the zero-filled page just written
  if (f != nullptr && page_id_out != nullptr) *page_id_out = pid;
  return f;
}

bool BufferPool::UnpinPage(PageId page_id, bool is_dirty) {
  auto it = table_.find(page_id);
  if (it == table_.end()) return false;
  Frame& f = frames_[it->second];
  if (is_dirty) f.dirty = true;
  if (f.pin_count > 0) f.pin_count--;
  return true;
}

bool BufferPool::FlushPage(PageId page_id) {
  auto it = table_.find(page_id);
  if (it == table_.end()) return false;
  Frame& f = frames_[it->second];
  if (f.valid && f.dirty) {
    dm_->WritePage(f.page_id, f.data);
    f.dirty = false;
  }
  return true;
}

void BufferPool::FlushAll() {
  for (Frame& f : frames_) {
    if (f.valid && f.dirty) {
      dm_->WritePage(f.page_id, f.data);
      f.dirty = false;
    }
  }
  dm_->Sync();
}

}  // namespace minidb
