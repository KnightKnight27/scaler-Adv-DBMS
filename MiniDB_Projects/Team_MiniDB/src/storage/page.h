#pragma once

#include <cstring>

#include "common/types.h"

namespace minidb {

// A Page is one fixed-size frame of memory that mirrors exactly one disk block.
// It carries only the raw bytes plus its on-disk identity; the buffer pool keeps
// the bookkeeping (pin count, dirty flag, usage count) in its own Frame struct,
// and SlottedPage / B+Tree node code interprets the bytes. Keeping Page this thin
// means any page *type* can live in any frame.
class Page {
public:
    Page() { reset(); }

    char*       data()       { return data_; }
    const char* data() const { return data_; }

    PageId page_id() const     { return page_id_; }
    void   set_page_id(PageId id) { page_id_ = id; }

    void reset() {
        std::memset(data_, 0, PAGE_SIZE);
        page_id_ = INVALID_PAGE_ID;
    }

private:
    char   data_[PAGE_SIZE];
    PageId page_id_ = INVALID_PAGE_ID;
};

} // namespace minidb
