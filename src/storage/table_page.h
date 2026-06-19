#pragma once

#include <cstdint>

#include "storage/page.h"
#include "storage/tuple.h"

namespace minidb {

// A slotted-page view over a raw buffer-pool Page. Tuples grow downward from
// the end of the page; a slot directory grows upward right after the header.
// This indirection lets a tuple be deleted (slot tombstoned) or relocated
// without changing its slot number, so an RID stays stable.
//
//   +-----------------------------------------------------------+
//   | header: next_page_id | num_slots | free_space_ptr         |
//   | slot[0] {off,len} | slot[1] | slot[2] | ...   --> grows up |
//   |                  ... free space ...                        |
//   |  ... tuple2 | tuple1 | tuple0                <-- grows down|
//   +-----------------------------------------------------------+
//
// A slot whose offset == 0 is a tombstone (offset 0 is inside the header, so a
// live tuple is never stored there).
class TablePage {
 public:
  static constexpr uint16_t SIZE_HEADER = 8;  // next(4) + num_slots(2) + free_ptr(2)
  static constexpr uint16_t SIZE_SLOT = 4;    // offset(2) + len(2)

  static void Init(Page *page);

  static page_id_t GetNextPageId(const Page *page);
  static void SetNextPageId(Page *page, page_id_t next);
  static uint16_t GetNumSlots(const Page *page);

  // Append a tuple; false if the page lacks room. Sets *slot on success.
  static bool InsertTuple(Page *page, const Tuple &t, slot_id_t *slot);
  // Read slot into *out; false if out of range or tombstoned.
  static bool GetTuple(const Page *page, slot_id_t slot, Tuple *out);
  static bool MarkDelete(Page *page, slot_id_t slot);
  static bool IsDeleted(const Page *page, slot_id_t slot);

  // Read / restore a slot's byte offset. A delete only zeros the offset (the
  // tuple bytes are never reclaimed), so saving the offset before a delete and
  // writing it back later un-tombstones the tuple in place — used to roll back
  // a deletion during transaction abort.
  static uint16_t GetSlotOffset(const Page *page, slot_id_t slot);
  static void SetSlotOffset(Page *page, slot_id_t slot, uint16_t offset);

  static uint16_t FreeSpace(const Page *page);
  // Largest tuple that can ever fit on an empty page.
  static uint16_t MaxTupleSize() { return PAGE_SIZE - SIZE_HEADER - SIZE_SLOT; }
};

}  // namespace minidb
