#pragma once

#include <cstdint>
#include <functional>

#include "common/config.h"

namespace axiomdb {

// ---------------------------------------------------------------------------
// RID -- Record Identifier.  The physical address of a tuple in the heap file:
// which page it lives on, and which slot within that page's slot array.  The
// B+tree index stores RIDs as its payload (key -> RID), so a point lookup is
// "search the tree for the key, then fetch the tuple at the resulting RID".
// ---------------------------------------------------------------------------

struct RID {
  page_id_t page_id = INVALID_PAGE_ID;
  slot_id_t slot = 0;

  bool operator==(const RID& o) const { return page_id == o.page_id && slot == o.slot; }
  bool operator!=(const RID& o) const { return !(*this == o); }
  bool valid() const { return page_id != INVALID_PAGE_ID; }
};

}  // namespace axiomdb

// Allow RID as a key in unordered_map (used by the lock manager / executors).
template <>
struct std::hash<axiomdb::RID> {
  size_t operator()(const axiomdb::RID& r) const noexcept {
    return (static_cast<size_t>(static_cast<uint32_t>(r.page_id)) << 16) ^ r.slot;
  }
};
