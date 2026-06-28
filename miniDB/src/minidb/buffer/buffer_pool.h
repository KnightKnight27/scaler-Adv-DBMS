#pragma once

#include <list>
#include <memory>
#include <unordered_map>

#include "minidb/storage/disk_manager.h"

namespace minidb {

class BufferPool {
 public:
  BufferPool(DiskManager& disk, std::size_t capacity);
  ~BufferPool();

  Page& NewPage();
  Page& FetchPage(PageId page_id);
  void UnpinPage(PageId page_id, bool dirty);
  void FlushPage(PageId page_id);
  void FlushAll();

 private:
  struct Frame {
    Page page;
    int pin_count{0};
    bool dirty{false};
  };

  void Touch(PageId page_id);
  void EvictIfNeeded();

  DiskManager& disk_;
  std::size_t capacity_;
  std::unordered_map<PageId, std::unique_ptr<Frame>> frames_;
  std::list<PageId> lru_;
};

}  // namespace minidb
