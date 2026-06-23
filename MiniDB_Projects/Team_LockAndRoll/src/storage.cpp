#include "storage.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>

namespace minidb {

DiskManager::DiskManager(std::string dir) : dir_(std::move(dir)) {
  ::mkdir(dir_.c_str(), 0755);
}

DiskManager::~DiskManager() {
  for (auto& f : files_)
    if (f->fd >= 0) ::close(f->fd);
}

file_id_t DiskManager::open_file(const std::string& name) {
  std::lock_guard<std::mutex> lk(meta_mtx_);
  auto it = by_name_.find(name);
  if (it != by_name_.end()) return it->second;

  auto e = std::make_unique<FileEntry>();
  e->path = dir_ + "/" + name;
  e->fd = ::open(e->path.c_str(), O_RDWR | O_CREAT, 0644);
  if (e->fd < 0) throw DBException("cannot open file " + e->path);
  off_t sz = ::lseek(e->fd, 0, SEEK_END);
  e->pages = static_cast<page_id_t>(sz / PAGE_SIZE);
  file_id_t fid = static_cast<file_id_t>(files_.size());
  by_name_[name] = fid;
  files_.push_back(std::move(e));
  return fid;
}

DiskManager::FileEntry* DiskManager::entry(file_id_t fid) const {
  std::lock_guard<std::mutex> lk(meta_mtx_);
  return files_.at(fid).get();
}

void DiskManager::read_page(file_id_t fid, page_id_t pid, char* dest) {
  FileEntry* f = entry(fid);
  off_t off = static_cast<off_t>(pid) * PAGE_SIZE;
  ssize_t n = ::pread(f->fd, dest, PAGE_SIZE, off);
  if (n < 0) throw DBException("read_page failed");
  if (n < PAGE_SIZE) std::memset(dest + n, 0, PAGE_SIZE - n);
}

void DiskManager::write_page(file_id_t fid, page_id_t pid, const char* src) {
  FileEntry* f = entry(fid);
  off_t off = static_cast<off_t>(pid) * PAGE_SIZE;
  ssize_t n = ::pwrite(f->fd, src, PAGE_SIZE, off);
  if (n != PAGE_SIZE) throw DBException("write_page failed");
  if (pid + 1 > f->pages.load()) f->pages.store(pid + 1);
}

page_id_t DiskManager::allocate_page(file_id_t fid) {
  page_id_t pid = entry(fid)->pages.load();
  char zero[PAGE_SIZE] = {0};
  write_page(fid, pid, zero);
  return pid;
}

page_id_t DiskManager::num_pages(file_id_t fid) const { return entry(fid)->pages.load(); }

void DiskManager::sync() {
  std::lock_guard<std::mutex> lk(meta_mtx_);
  for (auto& f : files_)
    if (f->fd >= 0) ::fsync(f->fd);
}

BufferPool::BufferPool(DiskManager* dm, size_t num_frames) : dm_(dm) {
  frames_.reserve(num_frames);
  for (size_t i = 0; i < num_frames; i++) {
    frames_.push_back(std::make_unique<Page>());
    free_list_.push_back(static_cast<int>(i));
  }
}

int BufferPool::find_victim() {
  if (!free_list_.empty()) {
    int f = free_list_.back();
    free_list_.pop_back();
    return f;
  }
  for (auto it = lru_.rbegin(); it != lru_.rend(); ++it) {
    int frame = *it;
    Page* p = frames_[frame].get();
    if (p->pin_count_ == 0) {
      if (p->is_dirty_) {
        // wal rule: flush log before writing the page
        if (log_flusher_) log_flusher_->flush_to(slotted::get_lsn(p->data()));
        dm_->write_page(p->file_id_, p->page_id_, p->data());
      }
      page_table_.erase(key(p->file_id_, p->page_id_));
      lru_.erase(lru_pos_[frame]);
      lru_pos_.erase(frame);
      return frame;
    }
  }
  return -1;
}

Page* BufferPool::fetch_page(file_id_t fid, page_id_t pid) {
  std::lock_guard<std::mutex> lk(latch_);
  uint64_t k = key(fid, pid);
  auto it = page_table_.find(k);
  if (it != page_table_.end()) {
    int frame = it->second;
    Page* p = frames_[frame].get();
    p->pin_count_++;
    lru_.erase(lru_pos_[frame]);
    lru_.push_front(frame);
    lru_pos_[frame] = lru_.begin();
    hits_++;
    return p;
  }
  misses_++;
  int frame = find_victim();
  if (frame < 0) throw DBException("buffer pool is full (all pages pinned)");
  Page* p = frames_[frame].get();
  p->reset();
  p->file_id_ = fid;
  p->page_id_ = pid;
  dm_->read_page(fid, pid, p->data());
  p->pin_count_ = 1;
  page_table_[k] = frame;
  lru_.push_front(frame);
  lru_pos_[frame] = lru_.begin();
  return p;
}

Page* BufferPool::new_page(file_id_t fid, page_id_t* out_pid) {
  page_id_t pid;
  {
    std::lock_guard<std::mutex> lk(latch_);
    pid = dm_->allocate_page(fid);
  }
  Page* p = fetch_page(fid, pid);
  slotted::init(p->data());
  p->is_dirty_ = true;
  *out_pid = pid;
  return p;
}

void BufferPool::unpin_page(file_id_t fid, page_id_t pid, bool dirty) {
  std::lock_guard<std::mutex> lk(latch_);
  auto it = page_table_.find(key(fid, pid));
  if (it == page_table_.end()) return;
  Page* p = frames_[it->second].get();
  if (p->pin_count_ > 0) p->pin_count_--;
  if (dirty) p->is_dirty_ = true;
}

void BufferPool::flush_page(file_id_t fid, page_id_t pid) {
  std::lock_guard<std::mutex> lk(latch_);
  auto it = page_table_.find(key(fid, pid));
  if (it == page_table_.end()) return;
  Page* p = frames_[it->second].get();
  if (p->is_dirty_) {
    if (log_flusher_) log_flusher_->flush_to(slotted::get_lsn(p->data()));
    dm_->write_page(fid, pid, p->data());
    p->is_dirty_ = false;
  }
}

void BufferPool::reset_discard() {
  std::lock_guard<std::mutex> lk(latch_);
  page_table_.clear();
  lru_.clear();
  lru_pos_.clear();
  free_list_.clear();
  for (size_t i = 0; i < frames_.size(); i++) {
    frames_[i]->reset();
    free_list_.push_back(static_cast<int>(i));
  }
}

void BufferPool::flush_all() {
  std::lock_guard<std::mutex> lk(latch_);
  for (auto& [k, frame] : page_table_) {
    Page* p = frames_[frame].get();
    if (p->is_dirty_) {
      if (log_flusher_) log_flusher_->flush_to(slotted::get_lsn(p->data()));
      dm_->write_page(p->file_id_, p->page_id_, p->data());
      p->is_dirty_ = false;
    }
  }
}

}  // namespace minidb
