#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "common/config.h"

namespace axiomdb {

// ---------------------------------------------------------------------------
// SlottedPageLayout - Slotted page layout for records.
//
//   +-----------------------------------------------------------+
//   | PageHeader (16 B): lsn, next_page_id, num_slots, free_ptr  |
//   +-----------------------------------------------------------+
//   | slot[0] | slot[1] | ... | slot[n-1]   --> grows forward    |
//   +-----------------------------------------------------------+
//   |                     free space                            |
//   +-----------------------------------------------------------+
//   | <-- tuple data grows backward from the end of the page    |
//   +-----------------------------------------------------------+
//
// Each slot is (offset:u16, length:u16).  A slot with offset == 0 is a
// tombstone (the record was deleted): the slot id stays valid forever so that
// outstanding RIDs never dangle, but the bytes are not reclaimed (no in-page
// compaction -- a stated trade-off; reclamation would require rewriting the
// page and rewriting every RID that points into it).
//
// SlottedPageLayout is a *view*: it does not own memory, it interprets a PAGE_SIZE
// byte buffer (a buffer-pool frame, or a test-local array).  All mutators
// assume the caller has the page pinned and will mark the frame dirty.
// ---------------------------------------------------------------------------

class SlottedPageLayout {
 public:
  // Header field byte offsets within the page.
  static constexpr size_t kLsnOffset = 0;          // u64
  static constexpr size_t kNextPageOffset = 8;     // i32
  static constexpr size_t kNumSlotsOffset = 12;    // u16
  static constexpr size_t kFreePtrOffset = 14;     // u16
  static constexpr size_t kHeaderSize = 16;
  static constexpr size_t kSlotSize = 4;           // u16 offset + u16 length
  static constexpr uint16_t kDeadOffset = 0;       // offset value marking a tombstone

  explicit SlottedPageLayout(char* data) : data_(data) {}

  // Initialise a brand-new empty page.
  void init();

  // --- record operations -------------------------------------------------
  // Append `record` and return its slot id, or nullopt if it does not fit.
  std::optional<slot_id_t> insert(std::string_view record);

  // Fetch the bytes of a live record; nullopt if the slot id is out of range
  // or tombstoned.
  std::optional<std::string_view> get(slot_id_t slot) const;

  // In-place overwrite a record with one of the SAME length (used by updates
  // that don't change size, e.g. a recovery redo writing an after-image).
  // Returns false if the slot is dead/out-of-range or sizes differ.
  bool update_in_place(slot_id_t slot, std::string_view record);

  // Tombstone a record.  Returns false if already dead / out of range.
  bool erase(slot_id_t slot);

  bool is_live(slot_id_t slot) const;

  // --- header / space ----------------------------------------------------
  uint16_t num_slots() const { return load_u16_at(kNumSlotsOffset); }
  uint16_t free_space_ptr() const { return load_u16_at(kFreePtrOffset); }

  page_id_t next_page_id() const;
  void set_next_page_id(page_id_t pid);

  uint64_t lsn() const;
  void set_lsn(uint64_t lsn);

  // Largest record (in bytes) that insert() could currently accept, accounting
  // for the slot it would also need to allocate.
  size_t free_space_for_insert() const;

 private:
  uint16_t load_u16_at(size_t off) const;
  void store_u16_at(size_t off, uint16_t v);

  size_t slot_array_end() const { return kHeaderSize + size_t(num_slots()) * kSlotSize; }
  void read_slot(slot_id_t slot, uint16_t& offset, uint16_t& length) const;
  void write_slot(slot_id_t slot, uint16_t offset, uint16_t length);

  char* data_;
};

}  // namespace axiomdb
