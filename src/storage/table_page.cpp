#include "storage/table_page.h"

#include <cstring>

namespace minidb {

// --- little byte helpers over the page buffer at fixed offsets --------------
static uint16_t RdU16(const char *p, size_t off) {
  uint16_t v;
  std::memcpy(&v, p + off, 2);
  return v;
}
static void WrU16(char *p, size_t off, uint16_t v) { std::memcpy(p + off, &v, 2); }
static int32_t RdI32(const char *p, size_t off) {
  int32_t v;
  std::memcpy(&v, p + off, 4);
  return v;
}
static void WrI32(char *p, size_t off, int32_t v) { std::memcpy(p + off, &v, 4); }

// Header field offsets.
static constexpr size_t OFF_NEXT = 0;       // i32 next page id
static constexpr size_t OFF_NUM_SLOTS = 4;  // u16
static constexpr size_t OFF_FREE_PTR = 6;   // u16, where tuple bytes start

static size_t SlotOff(uint16_t slot) { return TablePage::SIZE_HEADER + slot * TablePage::SIZE_SLOT; }

void TablePage::Init(Page *page) {
  char *d = page->GetData();
  WrI32(d, OFF_NEXT, INVALID_PAGE_ID);
  WrU16(d, OFF_NUM_SLOTS, 0);
  WrU16(d, OFF_FREE_PTR, PAGE_SIZE);
}

page_id_t TablePage::GetNextPageId(const Page *page) { return RdI32(page->GetData(), OFF_NEXT); }
void TablePage::SetNextPageId(Page *page, page_id_t next) { WrI32(page->GetData(), OFF_NEXT, next); }
uint16_t TablePage::GetNumSlots(const Page *page) { return RdU16(page->GetData(), OFF_NUM_SLOTS); }

uint16_t TablePage::FreeSpace(const Page *page) {
  const char *d = page->GetData();
  uint16_t free_ptr = RdU16(d, OFF_FREE_PTR);
  uint16_t num_slots = RdU16(d, OFF_NUM_SLOTS);
  uint16_t slot_end = SIZE_HEADER + num_slots * SIZE_SLOT;
  return free_ptr > slot_end ? free_ptr - slot_end : 0;
}

bool TablePage::InsertTuple(Page *page, const Tuple &t, slot_id_t *slot) {
  char *d = page->GetData();
  uint16_t len = static_cast<uint16_t>(t.Size());
  if (len == 0 || len > MaxTupleSize()) return false;
  // Need room for the tuple bytes plus one new slot entry.
  if (FreeSpace(page) < len + SIZE_SLOT) return false;

  uint16_t num_slots = RdU16(d, OFF_NUM_SLOTS);
  uint16_t free_ptr = RdU16(d, OFF_FREE_PTR);
  uint16_t new_off = free_ptr - len;

  std::memcpy(d + new_off, t.Data(), len);
  WrU16(d, SlotOff(num_slots), new_off);
  WrU16(d, SlotOff(num_slots) + 2, len);
  WrU16(d, OFF_FREE_PTR, new_off);
  WrU16(d, OFF_NUM_SLOTS, num_slots + 1);

  *slot = static_cast<slot_id_t>(num_slots);
  return true;
}

bool TablePage::IsDeleted(const Page *page, slot_id_t slot) {
  const char *d = page->GetData();
  if (slot >= RdU16(d, OFF_NUM_SLOTS)) return true;
  return RdU16(d, SlotOff(slot)) == 0;  // offset 0 == tombstone
}

bool TablePage::GetTuple(const Page *page, slot_id_t slot, Tuple *out) {
  const char *d = page->GetData();
  if (slot >= RdU16(d, OFF_NUM_SLOTS)) return false;
  uint16_t off = RdU16(d, SlotOff(slot));
  uint16_t len = RdU16(d, SlotOff(slot) + 2);
  if (off == 0) return false;  // tombstone
  out->SetData(d + off, len);
  return true;
}

bool TablePage::MarkDelete(Page *page, slot_id_t slot) {
  char *d = page->GetData();
  if (slot >= RdU16(d, OFF_NUM_SLOTS)) return false;
  if (RdU16(d, SlotOff(slot)) == 0) return false;  // already deleted
  WrU16(d, SlotOff(slot), 0);                       // tombstone (keep length field)
  return true;
}

uint16_t TablePage::GetSlotOffset(const Page *page, slot_id_t slot) {
  const char *d = page->GetData();
  if (slot >= RdU16(d, OFF_NUM_SLOTS)) return 0;
  return RdU16(d, SlotOff(slot));
}

void TablePage::SetSlotOffset(Page *page, slot_id_t slot, uint16_t offset) {
  char *d = page->GetData();
  if (slot >= RdU16(d, OFF_NUM_SLOTS)) return;
  WrU16(d, SlotOff(slot), offset);
}

}  // namespace minidb
