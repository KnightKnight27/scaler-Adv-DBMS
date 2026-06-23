#pragma once

#include <cstddef>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "minidb/storage/disk_manager.h"
#include "minidb/storage/page.h"

namespace minidb {

class BufferPoolManager {
 public:
  class PageGuard {
   public:
    PageGuard() = default;
    PageGuard(BufferPoolManager *manager, Page *page);
    ~PageGuard();
    PageGuard(const PageGuard &) = delete;
    PageGuard &operator=(const PageGuard &) = delete;
    PageGuard(PageGuard &&other) noexcept;
    PageGuard &operator=(PageGuard &&other) noexcept;

    Page &Get() { return *page_; }
    const Page &Get() const { return *page_; }
    Page *operator->() { return page_; }
    const Page *operator->() const { return page_; }
    void MarkDirty() { dirty_ = true; }
    explicit operator bool() const { return page_ != nullptr; }

   private:
    void Release();
    BufferPoolManager *manager_{nullptr};
    Page *page_{nullptr};
    bool dirty_{false};
  };

  explicit BufferPoolManager(DiskManager &disk, std::size_t pool_size = 16);
  ~BufferPoolManager();

  PageGuard NewPage();
  PageGuard FetchPage(PageId page_id);
  void FlushPage(PageId page_id);
  void FlushAll();
  std::size_t CachedPages() const;
  std::size_t DiskPageCount() const { return disk_.PageCount(); }

 private:
  friend class PageGuard;
  Page *LoadPage(PageId page_id, bool is_new);
  void Unpin(PageId page_id, bool dirty);
  void Touch(PageId page_id);
  void EvictOne();

  DiskManager &disk_;
  std::size_t pool_size_;
  mutable std::mutex mutex_;
  std::unordered_map<PageId, std::unique_ptr<Page>> pages_;
  std::list<PageId> lru_;
  std::unordered_map<PageId, std::list<PageId>::iterator> lru_positions_;
};

}  // namespace minidb
