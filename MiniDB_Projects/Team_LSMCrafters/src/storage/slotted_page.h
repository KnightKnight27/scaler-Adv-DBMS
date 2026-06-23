#pragma once
#include <cstdint>
#include <vector>
#include "storage/page.h"

namespace minidb {

// One slot-directory entry. A length of 0 marks a deleted (tombstoned) tuple;
// we keep the slot so existing RIDs stay valid.
struct Slot {
  uint16_t offset;
  uint16_t length;
};

// A view over a Page's bytes implementing a slotted-page layout:
//
//   [ PageHeader ][ slot dir -> ........... <- tuple data ]
//
// The slot directory grows forward right after the header; tuple bytes grow
// backward from the end of the page. SlottedPage owns no memory; it just
// interprets the page handed to it.
class SlottedPage {
 public:
  explicit SlottedPage(Page* page) : page_(page) {}

  void init();  // format a fresh, empty page

  // Copies bytes into the page. Returns false (without changing the page) if
  // there is not enough free space; otherwise sets out_slot to the new slot.
  bool insert_tuple(const std::vector<char>& bytes, uint16_t& out_slot);

  // Returns the tuple bytes, or an empty vector if the slot is tombstoned.
  std::vector<char> get_tuple(uint16_t slot) const;

  bool     delete_tuple(uint16_t slot);  // mark the slot as a tombstone
  uint16_t slot_count() const;

  PageId next_page() const;
  void   set_next_page(PageId id);

 private:
  PageHeader* header() const;
  Slot*       slot_dir() const;
  std::size_t free_space() const;

  Page* page_;
};

}  // namespace minidb
