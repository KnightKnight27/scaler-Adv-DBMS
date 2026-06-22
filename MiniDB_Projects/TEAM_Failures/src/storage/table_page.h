// ============================================================================
// table_page.h  --  Interprets a raw Page as a SLOTTED PAGE holding tuples.
//
// Tuples vary in length (VARCHARs differ), so we cannot store them in fixed
// rows.  The classic solution is the *slotted page*:
//
//   +----------------------------------------------------------+
//   | header | slot0 slot1 slot2 ... ->        free        <-  |
//   |        |  (slot directory grows right)   space   (tuple  |
//   |        |                                         bytes   |
//   |        |                                      grow left) |
//   +----------------------------------------------------------+
//
//   * The header + slot directory grow forward from the front.
//   * Tuple bytes grow backward from the end.
//   * Free space is the gap in the middle.
//   * Each slot stores (offset, length) of one tuple.  length == -1 marks a
//     deleted tuple (a "tombstone"); its slot number is never reused, which
//     keeps every RID stable for the lifetime of the page.
//
// A tuple's RID is (this page's id, its slot number).
// ============================================================================
#pragma once

#include "common/common.h"
#include "storage/page.h"

namespace minidb {

class TablePage {
 public:
  // Byte layout of the header at the very start of the page.
  static constexpr int OFF_NEXT_PAGE = 0;   // page_id_t : next page in the heap
  static constexpr int OFF_NUM_SLOTS = 4;   // int32     : number of slots
  static constexpr int OFF_FREE_PTR  = 8;   // int32     : start of tuple region
  static constexpr int HEADER_SIZE   = 12;
  static constexpr int SLOT_SIZE     = 8;   // (int32 offset, int32 length)
  static constexpr int TOMBSTONE     = -1;

  explicit TablePage(Page *page) : page_(page) {}

  // Prepare a freshly allocated page: no slots, all bytes are free.
  void init() {
    setNextPageId(INVALID_PAGE_ID);
    setInt(OFF_NUM_SLOTS, 0);
    setInt(OFF_FREE_PTR, PAGE_SIZE);   // tuple region is empty -> starts at end
  }

  // A page whose free pointer is still 0 was never init()-ed (e.g. a crash hit
  // between allocating the page and writing its header).  Recovery checks this.
  bool isUninitialized() const { return getInt(OFF_FREE_PTR) == 0; }

  page_id_t getNextPageId() const { return getInt(OFF_NEXT_PAGE); }
  void setNextPageId(page_id_t id) { setInt(OFF_NEXT_PAGE, id); }
  int  getNumSlots() const { return getInt(OFF_NUM_SLOTS); }

  // insert serialized tuple bytes.  On success writes the chosen slot number to
  // *out_slot and returns true.  Returns false if the page lacks room.
  bool insertTuple(const string &bytes, int *out_slot) {
    int len = static_cast<int>(bytes.size());
    int num_slots = getNumSlots();
    int free_ptr  = getInt(OFF_FREE_PTR);

    // We need room for the tuple bytes AND a new slot entry.
    int slot_dir_end = HEADER_SIZE + num_slots * SLOT_SIZE;
    int free_bytes   = free_ptr - slot_dir_end;
    if (free_bytes < len + SLOT_SIZE) return false;

    int new_off = free_ptr - len;                 // tuples grow downward
    memcpy(page_->data() + new_off, bytes.data(), len);
    setInt(OFF_FREE_PTR, new_off);

    int slot = num_slots;                          // append a new slot
    setSlot(slot, new_off, len);
    setInt(OFF_NUM_SLOTS, num_slots + 1);
    *out_slot = slot;
    return true;
  }

  // Fetch tuple bytes at `slot`.  Returns false if out of range or deleted.
  bool getTuple(int slot, string *out) const {
    if (slot < 0 || slot >= getNumSlots()) return false;
    int off, len;
    getSlot(slot, &off, &len);
    if (len == TOMBSTONE) return false;
    out->assign(page_->data() + off, len);
    return true;
  }

  // --- Recovery helpers ----------------------------------------------------
  // Used only by crash recovery to re-apply (REDO) or restore (UNDO) a tuple at
  // an EXACT slot recorded in the WAL.  Made idempotent so replaying the log
  // twice is harmless: if the slot already holds the tuple we just overwrite it
  // in place; if it is the next slot we append normally.
  void setTupleAtSlot(int slot, const string &bytes) {
    int num_slots = getNumSlots();
    if (slot == num_slots) {        // the common case during forward replay
      int dummy;
      insertTuple(bytes, &dummy);
      return;
    }
    if (slot < num_slots) {         // slot already exists -> overwrite in place
      int off, len;
      getSlot(slot, &off, &len);
      if (off == 0) {               // never materialized (e.g. was tombstoned
        return;                     // before its bytes were written) -> skip
      }
      memcpy(page_->data() + off, bytes.data(), bytes.size());
      setSlot(slot, off, static_cast<int>(bytes.size()));
    }
    // slot > num_slots cannot occur: WAL records arrive in slot order per page.
  }

  // Logically delete a tuple by tombstoning its slot.  Space is not reclaimed
  // (no intra-page compaction) -- a deliberate simplification.
  bool deleteTuple(int slot) {
    if (slot < 0 || slot >= getNumSlots()) return false;
    int off, len;
    getSlot(slot, &off, &len);
    if (len == TOMBSTONE) return false;
    setSlot(slot, off, TOMBSTONE);
    return true;
  }

 private:
  int  getInt(int off) const {
    int v; memcpy(&v, page_->data() + off, sizeof(v)); return v;
  }
  void setInt(int off, int v) {
    memcpy(page_->data() + off, &v, sizeof(v));
  }
  void getSlot(int slot, int *off, int *len) const {
    int base = HEADER_SIZE + slot * SLOT_SIZE;
    *off = getInt(base);
    *len = getInt(base + 4);
  }
  void setSlot(int slot, int off, int len) {
    int base = HEADER_SIZE + slot * SLOT_SIZE;
    setInt(base, off);
    setInt(base + 4, len);
  }

  Page *page_;
};

}  // namespace minidb
