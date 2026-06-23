#include "storage/slotted_page.h"
#include <cstring>

namespace minidb {

PageHeader* SlottedPage::header() const {
  return reinterpret_cast<PageHeader*>(page_->data.data());
}

Slot* SlottedPage::slot_dir() const {
  return reinterpret_cast<Slot*>(page_->data.data() + sizeof(PageHeader));
}

std::size_t SlottedPage::free_space() const {
  const PageHeader* h = header();
  std::size_t slot_dir_end = sizeof(PageHeader) + h->num_slots * sizeof(Slot);
  return h->free_ptr - slot_dir_end;
}

void SlottedPage::init() {
  PageHeader* h = header();
  h->next_page = kInvalidPage;
  h->num_slots = 0;
  h->free_ptr  = static_cast<uint16_t>(PAGE_SIZE);  // tuples grow down from the end
  h->page_lsn  = kInvalidLSN;
  h->rec_lsn   = kInvalidLSN;
}

bool SlottedPage::insert_tuple(const std::vector<char>& bytes, uint16_t& out_slot) {
  PageHeader* h = header();
  const std::size_t needed = bytes.size() + sizeof(Slot);  // tuple + its new slot
  if (free_space() < needed) return false;

  uint16_t offset = static_cast<uint16_t>(h->free_ptr - bytes.size());
  std::memcpy(page_->data.data() + offset, bytes.data(), bytes.size());

  out_slot = h->num_slots;
  slot_dir()[out_slot] = Slot{offset, static_cast<uint16_t>(bytes.size())};
  h->num_slots += 1;
  h->free_ptr   = offset;
  return true;
}

std::vector<char> SlottedPage::get_tuple(uint16_t slot) const {
  if (slot >= header()->num_slots) return {};
  Slot s = slot_dir()[slot];
  if (s.length == 0) return {};  // tombstone
  const char* start = page_->data.data() + s.offset;
  return std::vector<char>(start, start + s.length);
}

bool SlottedPage::delete_tuple(uint16_t slot) {
  if (slot >= header()->num_slots) return false;
  slot_dir()[slot].length = 0;  // tombstone; offset is left untouched
  return true;
}

uint16_t SlottedPage::slot_count() const { return header()->num_slots; }

PageId SlottedPage::next_page() const { return header()->next_page; }

void SlottedPage::set_next_page(PageId id) { header()->next_page = id; }

}  // namespace minidb
