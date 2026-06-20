#pragma once
#include <cstdint>
#include "common/config.h"

namespace minidb {

// Strongly-named integer aliases. Using distinct names (rather than bare int)
// documents intent at call sites and makes the code self-explaining in a viva.
using page_id_t  = int32_t;   // index of a page within the database file
using frame_id_t = int32_t;   // index of a frame within the buffer pool
using slot_id_t  = int32_t;   // index of a slot within a heap page
using txn_id_t   = uint64_t;  // monotonically increasing transaction id
using lsn_t      = uint64_t;  // log sequence number (WAL ordering)

// A RID (record identifier) uniquely locates a tuple on disk: which page,
// which slot. B+Tree leaves store RIDs so an index lookup can fetch the row.
struct RID {
    page_id_t page_id{INVALID_PAGE_ID};
    slot_id_t slot_id{-1};

    bool operator==(const RID &o) const {
        return page_id == o.page_id && slot_id == o.slot_id;
    }
    bool operator!=(const RID &o) const { return !(*this == o); }
};

} // namespace minidb
