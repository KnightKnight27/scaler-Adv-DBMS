#include "minidb/storage/page.h"

#include <cstring>

namespace minidb {
namespace {
constexpr std::uint32_t kHeapPageMagic = 0x4D444250;
}

Page::Page() { Reset(); }

void Page::Reset() {
  data_.fill(0);
  page_id_ = kInvalidPageId;
}

SlottedPage::SlottedPage(Page& page) : page_(page) {}

void SlottedPage::Init(PageId next_page) {
  std::memset(page_.Data(), 0, kPageSize);
  auto* h = header();
  h->magic = kHeapPageMagic;
  h->next_page = next_page;
  h->slot_count = 0;
  h->free_start = sizeof(Header);
  h->free_end = static_cast<std::uint16_t>(kPageSize);
}

PageId SlottedPage::NextPage() const { return header()->next_page; }

void SlottedPage::SetNextPage(PageId next_page) { header()->next_page = next_page; }

std::optional<SlotId> SlottedPage::Insert(std::string_view record) {
  if (record.size() > UINT16_MAX) {
    return std::nullopt;
  }
  const std::uint16_t required = static_cast<std::uint16_t>(record.size() + sizeof(Slot));
  if (FreeSpace() < required) {
    return std::nullopt;
  }

  auto* h = header();
  h->free_end = static_cast<std::uint16_t>(h->free_end - record.size());
  std::memcpy(page_.Data() + h->free_end, record.data(), record.size());

  SlotId slot_id = h->slot_count++;
  auto* s = slot(slot_id);
  s->offset = h->free_end;
  s->size = static_cast<std::uint16_t>(record.size());
  s->deleted = 0;
  h->free_start = static_cast<std::uint16_t>(sizeof(Header) + h->slot_count * sizeof(Slot));
  return slot_id;
}

std::optional<std::string> SlottedPage::Get(SlotId slot_id) const {
  if (slot_id >= header()->slot_count) {
    return std::nullopt;
  }
  const auto* s = slot(slot_id);
  if (s->deleted != 0) {
    return std::nullopt;
  }
  return std::string(page_.Data() + s->offset, page_.Data() + s->offset + s->size);
}

bool SlottedPage::Delete(SlotId slot_id) {
  if (slot_id >= header()->slot_count) {
    return false;
  }
  auto* s = slot(slot_id);
  if (s->deleted != 0) {
    return false;
  }
  s->deleted = 1;
  return true;
}

std::vector<std::pair<SlotId, std::string>> SlottedPage::Scan() const {
  std::vector<std::pair<SlotId, std::string>> rows;
  for (SlotId slot_id = 0; slot_id < header()->slot_count; ++slot_id) {
    auto record = Get(slot_id);
    if (record.has_value()) {
      rows.push_back({slot_id, *record});
    }
  }
  return rows;
}

std::uint16_t SlottedPage::FreeSpace() const {
  const auto* h = header();
  if (h->magic != kHeapPageMagic) {
    return static_cast<std::uint16_t>(kPageSize - sizeof(Header));
  }
  return static_cast<std::uint16_t>(h->free_end - h->free_start);
}

SlottedPage::Header* SlottedPage::header() {
  return reinterpret_cast<Header*>(page_.Data());
}

const SlottedPage::Header* SlottedPage::header() const {
  return reinterpret_cast<const Header*>(page_.Data());
}

SlottedPage::Slot* SlottedPage::slot(SlotId slot_id) {
  return reinterpret_cast<Slot*>(page_.Data() + sizeof(Header) + slot_id * sizeof(Slot));
}

const SlottedPage::Slot* SlottedPage::slot(SlotId slot_id) const {
  return reinterpret_cast<const Slot*>(page_.Data() + sizeof(Header) + slot_id * sizeof(Slot));
}

}  // namespace minidb
