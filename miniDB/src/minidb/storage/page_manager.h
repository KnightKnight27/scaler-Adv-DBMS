#pragma once

#include "minidb/buffer/buffer_pool.h"

namespace minidb {

class PageManager {
 public:
  explicit PageManager(BufferPool& buffer);

  PageId AllocateHeapPage(PageId next_page = kInvalidPageId);
  Page& FetchPage(PageId page_id);
  void Unpin(PageId page_id, bool dirty);
  void FlushAll();

 private:
  BufferPool& buffer_;
};

}  // namespace minidb
