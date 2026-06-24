#pragma once

#include <array>
#include <optional>
#include <vector>

#include "minidb/common/types.h"

namespace minidb {

class Page {
 public:
  Page();

  PageId page_id() const { return page_id_; }
  void set_page_id(PageId page_id) { page_id_ = page_id; }
  char* Data() { return data_.data(); }
  const char* Data() const { return data_.data(); }
  void Reset();

 private:
  PageId page_id_{kInvalidPageId};
  std::array<char, kPageSize> data_{};
};

class SlottedPage {
 public:
  explicit SlottedPage(Page& page);

  void Init(PageId next_page = kInvalidPageId);
  PageId NextPage() const;
  void SetNextPage(PageId next_page);
  std::optional<SlotId> Insert(std::string_view record);
  std::optional<std::string> Get(SlotId slot_id) const;
  bool Delete(SlotId slot_id);
  std::vector<std::pair<SlotId, std::string>> Scan() const;
  std::uint16_t FreeSpace() const;

 private:
  struct Header {
    std::uint32_t magic;
    PageId next_page;
    std::uint16_t slot_count;
    std::uint16_t free_start;
    std::uint16_t free_end;
  };

  struct Slot {
    std::uint16_t offset;
    std::uint16_t size;
    std::uint8_t deleted;
    std::uint8_t reserved[3];
  };

  Header* header();
  const Header* header() const;
  Slot* slot(SlotId slot_id);
  const Slot* slot(SlotId slot_id) const;

  Page& page_;
};

}  // namespace minidb
