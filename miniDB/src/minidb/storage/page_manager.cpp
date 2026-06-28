#include "minidb/storage/page_manager.h"

namespace minidb {

PageManager::PageManager(BufferPool& buffer) : buffer_(buffer) {}

PageId PageManager::AllocateHeapPage(PageId next_page) {
  Page& page = buffer_.NewPage();
  SlottedPage(page).Init(next_page);
  PageId page_id = page.page_id();
  buffer_.UnpinPage(page_id, true);
  return page_id;
}

Page& PageManager::FetchPage(PageId page_id) { return buffer_.FetchPage(page_id); }

void PageManager::Unpin(PageId page_id, bool dirty) { buffer_.UnpinPage(page_id, dirty); }

void PageManager::FlushAll() { buffer_.FlushAll(); }

}  // namespace minidb
