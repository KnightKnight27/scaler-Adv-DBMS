#pragma once
#include "page.h"

namespace minidb {

// Record ID: uniquely locates a tuple as (page, slot) within a heap file.
struct RID {
  page_id_t page_id = INVALID_PAGE_ID;
  int slot_num = -1;

  bool operator==(const RID &o) const { return page_id == o.page_id && slot_num == o.slot_num; }
  bool Valid() const { return page_id != INVALID_PAGE_ID && slot_num >= 0; }
};

}  // namespace minidb
