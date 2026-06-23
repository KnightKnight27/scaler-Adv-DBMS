#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// page.h  –  in-memory page frame
// ─────────────────────────────────────────────────────────────────────────────
#include "common/config.h"
#include "common/types.h"
#include <cstring>

namespace minidb {

/**
 * Page – one 4 KB block of data.
 *
 * The buffer pool holds an array of Page objects (frames).
 * The raw bytes are what get serialised to / from the .db file.
 * is_dirty_ and page_id_ are purely in-memory metadata.
 */
class Page {
public:
    Page()  { std::memset(data_, 0, PAGE_SIZE); }
    ~Page() = default;

    // Non-copyable: always refer to frames by pointer.
    Page(const Page&)            = delete;
    Page& operator=(const Page&) = delete;

    char*       GetData()  { return data_; }
    const char* GetData()  const { return data_; }

    page_id_t   GetPageId()          const { return page_id_;  }
    void        SetPageId(page_id_t p)     { page_id_  = p;    }

    bool        IsDirty()            const { return is_dirty_; }
    void        SetDirty(bool d)           { is_dirty_ = d;    }

    void        ResetMemory() { std::memset(data_, 0, PAGE_SIZE); }

private:
    char      data_[PAGE_SIZE]{};
    page_id_t page_id_  = INVALID_PAGE_ID;
    bool      is_dirty_ = false;
};

} // namespace minidb
