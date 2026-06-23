#include "minidb/storage/buffer_pool_manager.h"

#include <stdexcept>

#include "minidb/common/trace.h"

namespace minidb {

BufferPoolManager::PageGuard::PageGuard(BufferPoolManager *manager, Page *page)
    : manager_(manager), page_(page) {}

BufferPoolManager::PageGuard::~PageGuard() { Release(); }

BufferPoolManager::PageGuard::PageGuard(PageGuard &&other) noexcept
    : manager_(other.manager_), page_(other.page_), dirty_(other.dirty_) {
  other.manager_ = nullptr;
  other.page_ = nullptr;
}

BufferPoolManager::PageGuard &BufferPoolManager::PageGuard::operator=(
    PageGuard &&other) noexcept {
  if (this != &other) {
    Release();
    manager_ = other.manager_;
    page_ = other.page_;
    dirty_ = other.dirty_;
    other.manager_ = nullptr;
    other.page_ = nullptr;
  }
  return *this;
}

void BufferPoolManager::PageGuard::Release() {
  if (manager_ && page_) manager_->Unpin(page_->page_id, dirty_);
  manager_ = nullptr;
  page_ = nullptr;
}

BufferPoolManager::BufferPoolManager(DiskManager &disk, std::size_t pool_size)
    : disk_(disk), pool_size_(pool_size) {
  if (pool_size == 0) throw std::invalid_argument("buffer pool cannot be empty");
}

BufferPoolManager::~BufferPoolManager() {
  try {
    FlushAll();
  } catch (...) {
  }
}

BufferPoolManager::PageGuard BufferPoolManager::NewPage() {
  const PageId id = disk_.AllocatePage();
  std::scoped_lock lock(mutex_);
  return PageGuard(this, LoadPage(id, true));
}

BufferPoolManager::PageGuard BufferPoolManager::FetchPage(PageId page_id) {
  std::scoped_lock lock(mutex_);
  return PageGuard(this, LoadPage(page_id, false));
}

Page *BufferPoolManager::LoadPage(PageId page_id, bool is_new) {
  auto found = pages_.find(page_id);
  if (found != pages_.end()) {
    found->second->pin_count++;
    Touch(page_id);
    Trace::Log("BUFFER", "cache hit page " + std::to_string(page_id));
    return found->second.get();
  }
  if (pages_.size() >= pool_size_) EvictOne();
  auto page = std::make_unique<Page>();
  page->page_id = page_id;
  page->pin_count = 1;
  if (!is_new) disk_.ReadPage(page_id, page->data);
  auto *raw = page.get();
  pages_.emplace(page_id, std::move(page));
  lru_.push_front(page_id);
  lru_positions_[page_id] = lru_.begin();
  Trace::Log("BUFFER", "loaded page " + std::to_string(page_id));
  return raw;
}

void BufferPoolManager::Touch(PageId page_id) {
  auto pos = lru_positions_.find(page_id);
  if (pos != lru_positions_.end()) lru_.erase(pos->second);
  lru_.push_front(page_id);
  lru_positions_[page_id] = lru_.begin();
}

void BufferPoolManager::EvictOne() {
  for (auto it = lru_.rbegin(); it != lru_.rend(); ++it) {
    auto page_it = pages_.find(*it);
    if (page_it->second->pin_count != 0) continue;
    const PageId victim = *it;
    if (page_it->second->dirty) {
      disk_.WritePage(victim, page_it->second->data);
    }
    auto forward = std::next(it).base();
    lru_.erase(forward);
    lru_positions_.erase(victim);
    pages_.erase(page_it);
    Trace::Log("BUFFER", "evicted page " + std::to_string(victim));
    return;
  }
  throw std::runtime_error("all buffer frames are pinned");
}

void BufferPoolManager::Unpin(PageId page_id, bool dirty) {
  std::scoped_lock lock(mutex_);
  auto it = pages_.find(page_id);
  if (it == pages_.end() || it->second->pin_count == 0) return;
  it->second->pin_count--;
  it->second->dirty = it->second->dirty || dirty;
  Touch(page_id);
}

void BufferPoolManager::FlushPage(PageId page_id) {
  std::scoped_lock lock(mutex_);
  auto it = pages_.find(page_id);
  if (it == pages_.end()) return;
  if (it->second->dirty) {
    disk_.WritePage(page_id, it->second->data);
    it->second->dirty = false;
  }
}

void BufferPoolManager::FlushAll() {
  std::scoped_lock lock(mutex_);
  for (auto &[id, page] : pages_) {
    if (page->dirty) {
      disk_.WritePage(id, page->data);
      page->dirty = false;
    }
  }
  disk_.Flush();
}

std::size_t BufferPoolManager::CachedPages() const {
  std::scoped_lock lock(mutex_);
  return pages_.size();
}

}  // namespace minidb
