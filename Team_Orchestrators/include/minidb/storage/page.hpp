#pragma once
// Slotted-page layout used by heap files.
//
// Layout (offsets in bytes):
//   [0..2)            num_slots      (uint16)
//   [2..4)            free_ptr       (uint16) start of the record area; records
//                                    grow downward from PAGE_SIZE toward the slots
//   [4 .. 4+4*N)      slot directory; slot i = {offset:uint16, length:uint16}
//                                    length == 0 means the slot is deleted
//   [free_ptr..PAGE_SIZE)            packed record bytes
#include <cstdint>
#include <cstring>

namespace minidb {

using PageId = uint32_t;
static const PageId kInvalidPageId = 0xFFFFFFFFu;
static const size_t kPageSize = 4096;

// Non-owning view over a raw page buffer providing slotted-page operations.
class SlottedPage {
 public:
  explicit SlottedPage(uint8_t* data) : d_(data) {}

  void init() {
    set_u16(0, 0);                                    // num_slots
    set_u16(2, static_cast<uint16_t>(kPageSize));     // free_ptr
  }

  uint16_t num_slots() const { return get_u16(0); }

  // A freshly zeroed page has free_ptr == 0; an initialized one has it at the
  // page end. WAL redo uses this to init pages reconstructed from disk.
  bool is_initialized() const { return get_u16(2) != 0; }

  static constexpr uint16_t kInvalidSlot = 0xFFFF;

  // Inserts a record; returns its slot index, or kInvalidSlot if it doesn't fit.
  uint16_t insert(const uint8_t* rec, uint16_t len) {
    uint16_t slots = num_slots();
    size_t dir_end = kHeader + static_cast<size_t>(slots) * kSlotSize;
    size_t free_ptr = get_u16(2);
    size_t need = static_cast<size_t>(len) + kSlotSize;  // record + a new slot
    if (free_ptr < dir_end + need) return kInvalidSlot;
    size_t off = free_ptr - len;
    std::memcpy(d_ + off, rec, len);
    set_u16(2, static_cast<uint16_t>(off));
    write_slot(slots, static_cast<uint16_t>(off), len);
    set_u16(0, static_cast<uint16_t>(slots + 1));
    return slots;
  }

  // Places a record at an exact slot index (used by WAL redo, which must
  // reproduce the original RID). Slots between the current count and `slot`
  // are created as tombstones. Returns false if the record does not fit.
  bool insert_at(uint16_t slot, const uint8_t* rec, uint16_t len) {
    uint16_t slots = num_slots();
    uint16_t new_slot_count = slot >= slots ? static_cast<uint16_t>(slot + 1) : slots;
    size_t dir_end = kHeader + static_cast<size_t>(new_slot_count) * kSlotSize;
    size_t free_ptr = get_u16(2);
    if (free_ptr < dir_end + static_cast<size_t>(len)) return false;
    size_t off = free_ptr - len;
    std::memcpy(d_ + off, rec, len);
    set_u16(2, static_cast<uint16_t>(off));
    // Fill any gap slots as tombstones.
    for (uint16_t i = slots; i < slot; ++i) write_slot(i, 0, 0);
    write_slot(slot, static_cast<uint16_t>(off), len);
    if (new_slot_count > slots) set_u16(0, new_slot_count);
    return true;
  }

  // Returns the record bytes for a slot, or nullopt if deleted/out of range.
  bool get(uint16_t slot, const uint8_t*& out, uint16_t& len) const {
    if (slot >= num_slots()) return false;
    uint16_t off, l;
    read_slot(slot, off, l);
    if (l == 0) return false;
    out = d_ + off;
    len = l;
    return true;
  }

  bool remove(uint16_t slot) {
    if (slot >= num_slots()) return false;
    uint16_t off, l;
    read_slot(slot, off, l);
    if (l == 0) return false;
    write_slot(slot, off, 0);  // tombstone; space reclaimed on compaction later
    return true;
  }

 private:
  static constexpr size_t kHeader = 4;
  static constexpr size_t kSlotSize = 4;

  uint16_t get_u16(size_t off) const {
    return static_cast<uint16_t>(d_[off] | (d_[off + 1] << 8));
  }
  void set_u16(size_t off, uint16_t v) {
    d_[off] = static_cast<uint8_t>(v & 0xFF);
    d_[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
  }
  void read_slot(uint16_t i, uint16_t& off, uint16_t& len) const {
    size_t base = kHeader + static_cast<size_t>(i) * kSlotSize;
    off = get_u16(base);
    len = get_u16(base + 2);
  }
  void write_slot(uint16_t i, uint16_t off, uint16_t len) {
    size_t base = kHeader + static_cast<size_t>(i) * kSlotSize;
    set_u16(base, off);
    set_u16(base + 2, len);
  }

  uint8_t* d_;
};

}  // namespace minidb
