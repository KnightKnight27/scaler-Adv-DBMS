#include "storage/heap_file.h"
#include "storage/slotted_page.h"

namespace minidb {

HeapFile::HeapFile(BufferPool& buffer_pool, PageId first_page)
    : buffer_pool_(buffer_pool), first_page_(first_page), last_page_(first_page) {
  // Walk to the tail so appends start at the right page (handles a reopened file).
  PageId id = first_page_;
  while (id != kInvalidPage) {
    Page* p = buffer_pool_.fetch_page(id);
    PageId next = SlottedPage(p).next_page();
    buffer_pool_.unpin_page(id, false);
    if (next == kInvalidPage) { last_page_ = id; break; }
    id = next;
  }
}

RID HeapFile::insert(const std::vector<char>& tuple_bytes) {
  Page* page = buffer_pool_.fetch_page(last_page_);
  SlottedPage sp(page);
  uint16_t slot;
  if (sp.insert_tuple(tuple_bytes, slot)) {
    buffer_pool_.unpin_page(last_page_, true);
    return RID{last_page_, slot};
  }
  buffer_pool_.unpin_page(last_page_, false);

  // Current tail is full: allocate a new page and link it in.
  PageId new_id = buffer_pool_.allocate_page();
  Page* new_page = buffer_pool_.fetch_page(new_id);
  SlottedPage new_sp(new_page);
  new_sp.init();
  new_sp.insert_tuple(tuple_bytes, slot);  // fits: the page is empty
  buffer_pool_.unpin_page(new_id, true);

  Page* old_tail = buffer_pool_.fetch_page(last_page_);
  SlottedPage(old_tail).set_next_page(new_id);
  buffer_pool_.unpin_page(last_page_, true);

  last_page_ = new_id;
  return RID{new_id, slot};
}

std::vector<char> HeapFile::get(RID rid) const {
  Page* page = buffer_pool_.fetch_page(rid.page_id);
  std::vector<char> bytes = SlottedPage(page).get_tuple(rid.slot);
  buffer_pool_.unpin_page(rid.page_id, false);
  return bytes;
}

bool HeapFile::erase(RID rid) {
  Page* page = buffer_pool_.fetch_page(rid.page_id);
  bool ok = SlottedPage(page).delete_tuple(rid.slot);
  buffer_pool_.unpin_page(rid.page_id, ok);
  return ok;
}

HeapFile::Cursor::Cursor(BufferPool& buffer_pool, PageId start)
    : buffer_pool_(buffer_pool), page_id_(start) {}

bool HeapFile::Cursor::next(RID& out_rid, std::vector<char>& out_bytes) {
  while (page_id_ != kInvalidPage) {
    Page* page = buffer_pool_.fetch_page(page_id_);
    SlottedPage sp(page);
    uint16_t count = sp.slot_count();
    while (slot_ < count) {
      uint16_t cur = slot_++;
      std::vector<char> bytes = sp.get_tuple(cur);
      if (!bytes.empty()) {
        out_rid   = RID{page_id_, cur};
        out_bytes = std::move(bytes);
        buffer_pool_.unpin_page(page_id_, false);
        return true;
      }
    }
    PageId next = sp.next_page();
    buffer_pool_.unpin_page(page_id_, false);
    page_id_ = next;
    slot_    = 0;
  }
  return false;
}

}  // namespace minidb
