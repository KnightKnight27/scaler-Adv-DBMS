#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include "common/config.h"
#include "common/types.h"
#include "storage/page.h"

namespace minidb {

// Interprets a raw Page as a slotted heap page. Layout:
//
//   [ next_page_id(4) | num_slots(2) | free_ptr(2) ][ slot0 | slot1 | ... ]
//                                                          ... free ...
//                                          [ ...... tuple1 ][ tuple0 ]
//
// Slots grow forward from the header; tuples grow backward from the end of the
// page. Each slot is (offset:2, length:2). A deleted tuple keeps its slot but
// sets length = 0 (a tombstone), so a row's RID = (page_id, slot) stays stable.
class TablePage {
 public:
  static constexpr size_t HEADER_SIZE = 8;
  static constexpr size_t SLOT_SIZE   = 4;

  explicit TablePage(Page *page) : page_(page) {}

  void Init() {
    SetNextPageId(INVALID_PAGE_ID);
    SetNumSlots(0);
    SetFreePtr(PAGE_SIZE);
  }

  page_id_t GetNextPageId() const { return ReadI32(0); }
  void SetNextPageId(page_id_t id) { WriteI32(0, id); }

  uint16_t GetNumSlots() const { return ReadU16(4); }

  // Insert a serialized tuple. On success returns true and writes the slot
  // number to *slot. Fails (false) if the page has insufficient free space.
  bool InsertTuple(const std::string &bytes, slot_id_t *slot) {
    uint16_t num = GetNumSlots();
    uint16_t free_ptr = GetFreePtr();
    size_t need = bytes.size() + SLOT_SIZE;
    size_t slot_end = HEADER_SIZE + static_cast<size_t>(num) * SLOT_SIZE;
    if (free_ptr < slot_end + need) return false;  // not enough room

    uint16_t new_off = static_cast<uint16_t>(free_ptr - bytes.size());
    std::memcpy(page_->data() + new_off, bytes.data(), bytes.size());
    SetSlot(num, new_off, static_cast<uint16_t>(bytes.size()));
    SetFreePtr(new_off);
    SetNumSlots(num + 1);
    *slot = static_cast<slot_id_t>(num);
    return true;
  }

  // Read a tuple's bytes. Returns false if the slot is out of range or a
  // tombstone (deleted).
  bool GetTuple(slot_id_t slot, std::string *out) const {
    if (slot < 0 || slot >= static_cast<slot_id_t>(GetNumSlots())) return false;
    uint16_t off, len;
    GetSlot(slot, &off, &len);
    if (len == 0) return false;  // tombstone
    out->assign(page_->data() + off, len);
    return true;
  }

  // Tombstone a tuple in place. RID stays valid but reads return false.
  bool DeleteTuple(slot_id_t slot) {
    if (slot < 0 || slot >= static_cast<slot_id_t>(GetNumSlots())) return false;
    uint16_t off, len;
    GetSlot(slot, &off, &len);
    if (len == 0) return false;
    SetSlot(slot, off, 0);
    return true;
  }

 private:
  // --- raw field helpers ---
  int32_t ReadI32(size_t at) const {
    int32_t v;
    std::memcpy(&v, page_->data() + at, sizeof(v));
    return v;
  }
  void WriteI32(size_t at, int32_t v) {
    std::memcpy(page_->data() + at, &v, sizeof(v));
  }
  uint16_t ReadU16(size_t at) const {
    uint16_t v;
    std::memcpy(&v, page_->data() + at, sizeof(v));
    return v;
  }
  void WriteU16(size_t at, uint16_t v) {
    std::memcpy(page_->data() + at, &v, sizeof(v));
  }

  void SetNumSlots(uint16_t n) { WriteU16(4, n); }
  uint16_t GetFreePtr() const { return ReadU16(6); }
  void SetFreePtr(uint16_t p) { WriteU16(6, p); }

  void GetSlot(slot_id_t i, uint16_t *off, uint16_t *len) const {
    size_t base = HEADER_SIZE + static_cast<size_t>(i) * SLOT_SIZE;
    *off = ReadU16(base);
    *len = ReadU16(base + 2);
  }
  void SetSlot(slot_id_t i, uint16_t off, uint16_t len) {
    size_t base = HEADER_SIZE + static_cast<size_t>(i) * SLOT_SIZE;
    WriteU16(base, off);
    WriteU16(base + 2, len);
  }

  Page *page_;
};

}  // namespace minidb
