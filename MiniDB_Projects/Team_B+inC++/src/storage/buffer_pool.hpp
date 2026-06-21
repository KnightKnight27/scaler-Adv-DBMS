#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "../common/config.hpp"
#include "../common/types.hpp"

class DiskManager;

// caches pages in memory; clock-sweep eviction with pin_count + dirty
class BufferPool {
public:
    explicit BufferPool(DiskManager& disk, std::size_t num_frames = DEFAULT_POOL_FRAMES);

    // flushes dirty frames
    ~BufferPool();

    // load+pin page id, valid until unpin
    char* fetch_page(PageID id);

    // dirty=true if you wrote to it
    bool unpin_page(PageID id, bool dirty);

    // grow file by one page, return id
    PageID new_page();

    void flush_page(PageID id);
    void flush_all();

private:
    struct Frame {
        PageID        page_id = INVALID_PAGE_ID;
        int           pin_count = 0;
        bool          dirty = false;
        std::uint8_t  usage = 0;
        bool          valid = false;
        char          data[PAGE_SIZE];
    };

    DiskManager&                        disk_;
    std::vector<Frame>                  frames_;
    std::unordered_map<PageID, std::size_t> table_;  // page_id -> frame index
    std::size_t                         hand_ = 0;    // clock-sweep position

    std::size_t find_victim();  // throws if all pinned
};
