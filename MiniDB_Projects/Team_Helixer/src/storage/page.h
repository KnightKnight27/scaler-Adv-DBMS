#pragma once
#include <cstring>
#include "common/config.h"
#include "common/types.h"

namespace minidb {

// A Page is a fixed-size (PAGE_SIZE) block of memory that mirrors one page on
// disk. The buffer pool owns an array of these frames. Metadata (pin count,
// dirty flag, owning page id) lives alongside the raw bytes.
//
//   - pin_count_ : how many callers are currently using this frame. A frame
//                  may only be evicted when its pin count drops to zero.
//   - is_dirty_  : true if the in-memory copy differs from disk and must be
//                  flushed before eviction.
class Page {
public:
    Page() { reset(); }

    char       *data() { return data_; }
    const char *data() const { return data_; }

    page_id_t page_id() const { return page_id_; }
    int       pin_count() const { return pin_count_; }
    bool      is_dirty() const { return is_dirty_; }

    void set_page_id(page_id_t id) { page_id_ = id; }
    void set_dirty(bool d) { is_dirty_ = d; }
    void pin()   { ++pin_count_; }
    void unpin() { if (pin_count_ > 0) --pin_count_; }

    // Reset frame to a clean, unused state (used on eviction / new allocation).
    void reset() {
        std::memset(data_, 0, PAGE_SIZE);
        page_id_   = INVALID_PAGE_ID;
        pin_count_ = 0;
        is_dirty_  = false;
    }

private:
    char      data_[PAGE_SIZE];
    page_id_t page_id_{INVALID_PAGE_ID};
    int       pin_count_{0};
    bool      is_dirty_{false};
};

} // namespace minidb
