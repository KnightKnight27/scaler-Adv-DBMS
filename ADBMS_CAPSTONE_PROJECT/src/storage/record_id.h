#pragma once
#include "storage/page.h"

namespace minidb {

struct RecordId {
    PageId page_id_{INVALID_PAGE_ID};
    int slot_num_{-1};

    bool operator==(const RecordId& other) const {
        return page_id_ == other.page_id_ && slot_num_ == other.slot_num_;
    }
};

} // namespace minidb
