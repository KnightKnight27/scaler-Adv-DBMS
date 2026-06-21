#include "storage/slotted_page.h"

#include <cstring>

#include "common/serialize.h"

namespace walterdb {

uint16_t SlottedPage::load_u16_at(size_t off) const { return load_u16(data_ + off); }
void SlottedPage::store_u16_at(size_t off, uint16_t v) { store_u16(data_ + off, v); }

void SlottedPage::read_slot(slot_id_t slot, uint16_t& offset, uint16_t& length) const {
  const char* p = data_ + kHeaderSize + size_t(slot) * kSlotSize;
  offset = load_u16(p);
  length = load_u16(p + 2);
}

void SlottedPage::write_slot(slot_id_t slot, uint16_t offset, uint16_t length) {
  char* p = data_ + kHeaderSize + size_t(slot) * kSlotSize;
  store_u16(p, offset);
  store_u16(p + 2, length);
}

void SlottedPage::init() {
  std::memset(data_, 0, PAGE_SIZE);
  store_u64(data_ + kLsnOffset, 0);
  store_u32(data_ + kNextPageOffset, static_cast<uint32_t>(INVALID_PAGE_ID));
  store_u16_at(kNumSlotsOffset, 0);
  store_u16_at(kFreePtrOffset, static_cast<uint16_t>(PAGE_SIZE));
}

page_id_t SlottedPage::next_page_id() const {
  return static_cast<page_id_t>(load_u32(data_ + kNextPageOffset));
}
void SlottedPage::set_next_page_id(page_id_t pid) {
  store_u32(data_ + kNextPageOffset, static_cast<uint32_t>(pid));
}

uint64_t SlottedPage::lsn() const { return load_u64(data_ + kLsnOffset); }
void SlottedPage::set_lsn(uint64_t lsn) { store_u64(data_ + kLsnOffset, lsn); }

size_t SlottedPage::free_space_for_insert() const {
  size_t contig = size_t(free_space_ptr()) - slot_array_end();
  // An insert needs room for the record bytes AND a new 4-byte slot.
  return contig > kSlotSize ? contig - kSlotSize : 0;
}

std::optional<slot_id_t> SlottedPage::insert(std::string_view record) {
  const size_t len = record.size();
  if (len > free_space_for_insert()) return std::nullopt;

  uint16_t fp = free_space_ptr();
  uint16_t new_offset = static_cast<uint16_t>(fp - len);
  std::memcpy(data_ + new_offset, record.data(), len);

  slot_id_t slot = num_slots();
  write_slot(slot, new_offset, static_cast<uint16_t>(len));
  store_u16_at(kFreePtrOffset, new_offset);
  store_u16_at(kNumSlotsOffset, slot + 1);
  return slot;
}

std::optional<std::string_view> SlottedPage::get(slot_id_t slot) const {
  if (slot >= num_slots()) return std::nullopt;
  uint16_t off, len;
  read_slot(slot, off, len);
  if (off == kDeadOffset) return std::nullopt;  // tombstoned
  return std::string_view(data_ + off, len);
}

bool SlottedPage::is_live(slot_id_t slot) const {
  if (slot >= num_slots()) return false;
  uint16_t off, len;
  read_slot(slot, off, len);
  return off != kDeadOffset;
}

bool SlottedPage::update_in_place(slot_id_t slot, std::string_view record) {
  if (slot >= num_slots()) return false;
  uint16_t off, len;
  read_slot(slot, off, len);
  if (off == kDeadOffset || len != record.size()) return false;
  std::memcpy(data_ + off, record.data(), record.size());
  return true;
}

bool SlottedPage::erase(slot_id_t slot) {
  if (slot >= num_slots()) return false;
  uint16_t off, len;
  read_slot(slot, off, len);
  if (off == kDeadOffset) return false;  // already dead
  write_slot(slot, kDeadOffset, 0);      // tombstone: offset 0 is never valid for live data
  return true;
}

}  // namespace walterdb
