#pragma once
#include <cstring>
#include <string>
#include "common/config.h"
#include "storage/page.h"

namespace minidb {

// A slotted heap page laid over a raw buffer-pool Page.
//
// Layout:
//   [ header ][ slot[0] slot[1] ... ]            (grows forward from start)
//                                  ... free ...
//                              [ ... tuple1 ][ tuple0 ]  (grows backward from end)
//
// Header:  next_page_id (4) | num_slots (4) | free_ptr (4)
// Slot:    offset (4) | length (4)        length==0 => deleted (tombstone)
//
// Slots are never removed once allocated, so a RID (page_id, slot) stays
// valid for the life of the row -- important for index entries pointing at it.
class TablePage {
 public:
  static constexpr int HEADER_SIZE = 12;
  static constexpr int SLOT_SIZE = 8;

  explicit TablePage(Page* page) : page_(page) {}

  void Init() {
    SetNextPageId(INVALID_PAGE_ID);
    SetNumSlots(0);
    SetFreePtr(PAGE_SIZE);
  }

  page_id_t GetNextPageId() const { return ReadInt(0); }
  void SetNextPageId(page_id_t id) { WriteInt(0, id); }

  int GetNumSlots() const { return ReadInt(4); }

  // Try to insert a serialized tuple; returns the slot id or -1 if no room.
  int InsertTuple(const std::string& data) {
    int n = GetNumSlots();
    int len = static_cast<int>(data.size());
    int free_ptr = GetFreePtr();
    int slot_end = HEADER_SIZE + (n + 1) * SLOT_SIZE;  // after adding one slot
    if (free_ptr - len < slot_end) return -1;          // not enough space
    int new_off = free_ptr - len;
    std::memcpy(Data() + new_off, data.data(), len);
    SetSlot(n, new_off, len);
    SetFreePtr(new_off);
    SetNumSlots(n + 1);
    return n;
  }

  // Read tuple bytes at `slot`. Returns false if slot is out of range/deleted.
  bool GetTuple(int slot, std::string* out) const {
    if (slot < 0 || slot >= GetNumSlots()) return false;
    int off, len;
    GetSlot(slot, &off, &len);
    if (len == 0) return false;  // tombstone
    out->assign(Data() + off, len);
    return true;
  }

  // Logical delete: mark the slot as a tombstone (length 0).
  bool DeleteTuple(int slot) {
    if (slot < 0 || slot >= GetNumSlots()) return false;
    int off, len;
    GetSlot(slot, &off, &len);
    if (len == 0) return false;
    SetSlot(slot, off, 0);
    return true;
  }

 private:
  char* Data() { return page_->Data(); }
  const char* Data() const { return page_->Data(); }

  int GetFreePtr() const { return ReadInt(8); }
  void SetFreePtr(int p) { WriteInt(8, p); }
  void SetNumSlots(int n) { WriteInt(4, n); }

  void GetSlot(int i, int* off, int* len) const {
    *off = ReadInt(HEADER_SIZE + i * SLOT_SIZE);
    *len = ReadInt(HEADER_SIZE + i * SLOT_SIZE + 4);
  }
  void SetSlot(int i, int off, int len) {
    WriteInt(HEADER_SIZE + i * SLOT_SIZE, off);
    WriteInt(HEADER_SIZE + i * SLOT_SIZE + 4, len);
  }

  int ReadInt(int pos) const {
    int32_t v;
    std::memcpy(&v, Data() + pos, sizeof(v));
    return v;
  }
  void WriteInt(int pos, int32_t v) { std::memcpy(Data() + pos, &v, sizeof(v)); }

  Page* page_;
};

}  // namespace minidb
