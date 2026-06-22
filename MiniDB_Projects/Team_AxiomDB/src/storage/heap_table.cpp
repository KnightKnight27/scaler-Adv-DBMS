#include "storage/heap_table.h"

#include <stdexcept>

#include "buffer/page.h"
#include "storage/slotted_page_layout.h"

namespace axiomdb {

size_t HeapTable::max_record_size() {
  // One page minus the header and one slot.
  return PAGE_SIZE - SlottedPageLayout::kHeaderSize - SlottedPageLayout::kSlotSize;
}

HeapTable::HeapTable(BufferPoolManager* bpm, page_id_t first_page_id)
    : bpm_(bpm), first_page_id_(first_page_id), last_page_id_(first_page_id) {
  // Walk to the tail so inserts have a hint and don't re-scan each time.
  page_id_t pid = first_page_id_;
  while (pid != INVALID_PAGE_ID) {
    Page* p = bpm_->fetch_page(pid);
    if (!p) throw std::runtime_error("HeapTable: cannot fetch page while finding tail");
    SlottedPageLayout sp(p->data());
    page_id_t next = sp.next_page_id();
    bpm_->unpin_page(pid, false);
    if (next == INVALID_PAGE_ID) {
      last_page_id_ = pid;
      break;
    }
    pid = next;
  }
}

std::unique_ptr<HeapTable> HeapTable::create(BufferPoolManager* bpm) {
  page_id_t pid;
  Page* p = bpm->new_page(&pid);
  if (!p) throw std::runtime_error("HeapTable::create: no frame available");
  SlottedPageLayout sp(p->data());
  sp.init();
  bpm->unpin_page(pid, true);
  return std::make_unique<HeapTable>(bpm, pid);
}

RID HeapTable::insert(std::string_view record) {
  if (record.size() > max_record_size()) {
    throw std::runtime_error("HeapTable::insert: record exceeds page capacity (no overflow pages)");
  }

  // Try the current tail page first.
  Page* tail = bpm_->fetch_page(last_page_id_);
  if (!tail) throw std::runtime_error("HeapTable::insert: cannot fetch tail page");
  {
    SlottedPageLayout sp(tail->data());
    if (auto slot = sp.insert(record)) {
      bpm_->unpin_page(last_page_id_, true);
      return RID{last_page_id_, *slot};
    }
  }
  bpm_->unpin_page(last_page_id_, false);

  // Tail is full: allocate a new page, insert there, then link it in.
  page_id_t new_pid;
  Page* np = bpm_->new_page(&new_pid);
  if (!np) throw std::runtime_error("HeapTable::insert: no frame for new page");
  slot_id_t new_slot;
  {
    SlottedPageLayout nsp(np->data());
    nsp.init();
    auto slot = nsp.insert(record);  // guaranteed to fit on an empty page
    new_slot = *slot;
  }
  bpm_->unpin_page(new_pid, true);

  // Link previous tail -> new tail.
  Page* prev = bpm_->fetch_page(last_page_id_);
  if (!prev) throw std::runtime_error("HeapTable::insert: cannot re-fetch tail page to link");
  {
    SlottedPageLayout psp(prev->data());
    psp.set_next_page_id(new_pid);
  }
  bpm_->unpin_page(last_page_id_, true);
  last_page_id_ = new_pid;

  return RID{new_pid, new_slot};
}

std::optional<std::string> HeapTable::get(RID rid) const {
  Page* p = bpm_->fetch_page(rid.page_id);
  if (!p) return std::nullopt;
  SlottedPageLayout sp(p->data());
  auto rec = sp.get(rid.slot);
  std::optional<std::string> out;
  if (rec) out = std::string(*rec);
  bpm_->unpin_page(rid.page_id, false);
  return out;
}

bool HeapTable::erase(RID rid) {
  Page* p = bpm_->fetch_page(rid.page_id);
  if (!p) return false;
  SlottedPageLayout sp(p->data());
  bool ok = sp.erase(rid.slot);
  bpm_->unpin_page(rid.page_id, ok);
  return ok;
}

bool HeapTable::update_in_place(RID rid, std::string_view record) {
  Page* p = bpm_->fetch_page(rid.page_id);
  if (!p) return false;
  SlottedPageLayout sp(p->data());
  bool ok = sp.update_in_place(rid.slot, record);
  bpm_->unpin_page(rid.page_id, ok);
  return ok;
}

// ----- Cursor ---------------------------------------------------------------

HeapTable::Cursor::Cursor(BufferPoolManager* bpm, page_id_t start_page) : bpm_(bpm) {
  if (start_page == INVALID_PAGE_ID) return;
  page_id_ = start_page;
  page_ = bpm_->fetch_page(page_id_);
  if (!page_) {  // could not pin -> behave as end
    page_id_ = INVALID_PAGE_ID;
    return;
  }
  slot_ = 0;
  seek_live();
}

HeapTable::Cursor::~Cursor() { release(); }

void HeapTable::Cursor::release() {
  if (page_) {
    bpm_->unpin_page(page_id_, false);
    page_ = nullptr;
  }
  page_id_ = INVALID_PAGE_ID;
  value_ = {};
}

HeapTable::Cursor::Cursor(Cursor&& o) noexcept
    : bpm_(o.bpm_), page_id_(o.page_id_), page_(o.page_), slot_(o.slot_),
      rid_(o.rid_), value_(o.value_) {
  o.page_ = nullptr;
  o.page_id_ = INVALID_PAGE_ID;
}

HeapTable::Cursor& HeapTable::Cursor::operator=(Cursor&& o) noexcept {
  if (this != &o) {
    release();
    bpm_ = o.bpm_;
    page_id_ = o.page_id_;
    page_ = o.page_;
    slot_ = o.slot_;
    rid_ = o.rid_;
    value_ = o.value_;
    o.page_ = nullptr;
    o.page_id_ = INVALID_PAGE_ID;
  }
  return *this;
}

void HeapTable::Cursor::seek_live() {
  // Starting from (page_, slot_), advance to the next live slot, crossing into
  // subsequent pages as needed.  On success sets rid_/value_; on exhaustion
  // releases and becomes an end cursor.
  while (page_) {
    SlottedPageLayout sp(page_->data());
    uint16_t n = sp.num_slots();
    while (slot_ < n) {
      if (auto rec = sp.get(slot_)) {
        rid_ = RID{page_id_, slot_};
        value_ = *rec;  // view into the pinned frame; valid until we move on
        return;
      }
      ++slot_;
    }
    // Exhausted this page: move to the next in the chain.
    page_id_t next = sp.next_page_id();
    bpm_->unpin_page(page_id_, false);
    page_ = nullptr;
    if (next == INVALID_PAGE_ID) {
      page_id_ = INVALID_PAGE_ID;
      value_ = {};
      return;
    }
    page_id_ = next;
    page_ = bpm_->fetch_page(page_id_);
    slot_ = 0;
  }
}

void HeapTable::Cursor::next() {
  if (!page_) return;
  ++slot_;
  seek_live();
}

}  // namespace axiomdb
