// slotted_page.h — Phase A: slotted-page layout over a raw 4 KB page buffer.
//
// Layout (offsets in bytes from the start of the page's `data`):
//
//   0:                  PageHeader { uint16 num_slots; uint16 free_end; }
//   4:                  Slot[0] { uint16 offset; uint16 length }
//   4 + 4*i:            Slot[i]  ... (slot directory grows UP, toward higher addrs)
//   ...free space...
//   free_end .. 4096:   record bytes (grow DOWN, filled from the end of the page)
//
// `free_end` is the byte offset where the record area currently starts. A slot
// with length == 0 is a tombstone (deleted); we never compact, matching the
// project's tombstone philosophy. A freshly zeroed page has free_end == 0,
// which we treat as "uninitialised" and lazily reset to PAGE_SIZE.
#pragma once

#include <cstdint>
#include <cstring>

#include "page.h"  // PAGE_SIZE

// Thin, non-owning view over a page's 4096-byte `data` buffer. Construct one on
// the stack each time you touch a page; it stores no state of its own.
struct SlottedPage {
  char* d;

  static constexpr int kHeaderSize = 4;  // num_slots(2) + free_end(2)
  static constexpr int kSlotSize = 4;    // offset(2) + length(2)

  explicit SlottedPage(char* data) : d(data) { ensureInit(); }

  uint16_t numSlots() const { return u16(0); }
  uint16_t freeEnd() const { return u16(2); }

  // Bytes available for one more (record + slot) pair.
  int freeSpace() const {
    return static_cast<int>(freeEnd()) - (kHeaderSize + numSlots() * kSlotSize);
  }

  // Append a record; returns its slot id, or -1 if the page is full.
  int insertRecord(const char* rec, uint16_t len) {
    if (freeSpace() < static_cast<int>(len) + kSlotSize) return -1;
    uint16_t off = static_cast<uint16_t>(freeEnd() - len);
    std::memcpy(d + off, rec, len);
    setU16(2, off);  // free_end
    int slot = numSlots();
    setSlot(slot, off, len);
    setU16(0, static_cast<uint16_t>(slot + 1));  // num_slots
    return slot;
  }

  // Fetch a record. Returns false if `slot` is out of range; otherwise sets
  // `rec`/`len` (len == 0 means the slot is a tombstone).
  bool getRecord(int slot, const char*& rec, uint16_t& len) const {
    if (slot < 0 || slot >= numSlots()) return false;
    len = slotLength(slot);
    rec = d + slotOffset(slot);
    return true;
  }

  // Tombstone a slot (length := 0). The record bytes and offset are kept so the
  // delete can be undone by restoreRecord().
  void deleteRecord(int slot) {
    if (slot >= 0 && slot < numSlots()) setSlot(slot, slotOffset(slot), 0);
  }

  // Undo a deleteRecord(): restore the slot's original length.
  void restoreRecord(int slot, uint16_t len) {
    if (slot >= 0 && slot < numSlots()) setSlot(slot, slotOffset(slot), len);
  }

  uint16_t slotOffset(int i) const { return u16(kHeaderSize + i * kSlotSize); }
  uint16_t slotLength(int i) const { return u16(kHeaderSize + i * kSlotSize + 2); }

 private:
  uint16_t u16(int off) const {
    uint16_t v;
    std::memcpy(&v, d + off, 2);
    return v;
  }
  void setU16(int off, uint16_t v) { std::memcpy(d + off, &v, 2); }
  void setSlot(int i, uint16_t off, uint16_t len) {
    setU16(kHeaderSize + i * kSlotSize, off);
    setU16(kHeaderSize + i * kSlotSize + 2, len);
  }
  void ensureInit() {
    if (freeEnd() == 0) {  // zeroed/new page: records start at the very end
      setU16(0, 0);
      setU16(2, PAGE_SIZE);
    }
  }
};
