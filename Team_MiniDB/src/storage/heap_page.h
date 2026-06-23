#pragma once
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>

#include "page.h"

namespace minidb {

// Slotted-page layout for variable-length tuples:
//
//   [ num_slots:int32 | free_end:int32 | next_page_id:int32 | slot[0] | ... ]
//   [                      free space                            ]
//   [ ... tuple data (grows backward from PAGE_SIZE) ...          ]
//
// Each slot is {offset:int32, length:int32}. length == -1 marks a deleted
// (tombstoned) slot so RIDs referencing it stay stable for indexes/logs.
// next_page_id lives in the page header itself (not a side table) so the
// heap file's page chain survives a process restart / crash recovery.
class HeapPage {
 public:
  struct Slot {
    int32_t offset;
    int32_t length;
  };

  explicit HeapPage(char *raw) : raw_(raw) {}

  void Init() {
    NumSlots() = 0;
    FreeEnd() = PAGE_SIZE;
    NextPageId() = INVALID_PAGE_ID;
  }

  int32_t &NumSlots() { return *reinterpret_cast<int32_t *>(raw_); }
  int32_t &FreeEnd() { return *reinterpret_cast<int32_t *>(raw_ + sizeof(int32_t)); }
  page_id_t &NextPageId() { return *reinterpret_cast<page_id_t *>(raw_ + 2 * sizeof(int32_t)); }

  static constexpr size_t kHeaderSize = 3 * sizeof(int32_t);

  Slot *SlotArray() { return reinterpret_cast<Slot *>(raw_ + kHeaderSize); }

  // Largest tuple that could ever fit, even on a brand-new empty page (one
  // slot's worth of overhead reserved). Callers use this to reject an
  // oversized row up front instead of silently dropping it after
  // HeapFile::InsertTuple exhausts every page without finding room.
  static constexpr size_t MaxTupleSize() { return PAGE_SIZE - kHeaderSize - sizeof(Slot); }

  size_t FreeSpace() const {
    auto *self = const_cast<HeapPage *>(this);
    return self->FreeEnd() - kHeaderSize - self->NumSlots() * sizeof(Slot);
  }

  // Returns slot index of the inserted tuple, or -1 if not enough space.
  int InsertTuple(const char *data, int32_t len) {
    if (FreeSpace() < len + sizeof(Slot)) return -1;
    int32_t new_end = FreeEnd() - len;
    memcpy(raw_ + new_end, data, len);
    int idx = NumSlots();
    SlotArray()[idx] = {new_end, len};
    NumSlots()++;
    FreeEnd() = new_end;
    return idx;
  }

  bool GetTuple(int slot_num, std::string *out) {
    if (slot_num < 0 || slot_num >= NumSlots()) return false;
    Slot s = SlotArray()[slot_num];
    if (s.length < 0) return false;  // deleted
    out->assign(raw_ + s.offset, s.length);
    return true;
  }

  bool DeleteTuple(int slot_num) {
    if (slot_num < 0 || slot_num >= NumSlots()) return false;
    if (SlotArray()[slot_num].length < 0) return false;
    SlotArray()[slot_num].length = -1;
    return true;
  }

  // Undo of DeleteTuple: the tombstoned slot's bytes are never overwritten
  // by DeleteTuple, so restoring is just un-flipping the length sign back
  // to its original value (used by recovery's undo pass).
  bool RestoreTuple(int slot_num, int32_t original_length) {
    if (slot_num < 0 || slot_num >= NumSlots()) return false;
    SlotArray()[slot_num].length = original_length;
    return true;
  }

  int NumTupleSlots() { return NumSlots(); }

 private:
  char *raw_;
};

}  // namespace minidb
