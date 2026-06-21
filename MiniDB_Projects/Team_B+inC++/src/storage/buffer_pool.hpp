#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "../common/config.hpp"
#include "../common/types.hpp"

class DiskManager;

// The buffer pool caches a fixed set of pages in memory so the layers above
// never call the disk directly. Eviction uses clock-sweep (the PostgreSQL /
// lab-3 policy): each frame has a usage counter; the "hand" sweeps frames,
// decrementing usage until it finds one at 0 to evict. We extend the lab's
// pure cache with the two things a real pool needs:
//   - pin_count: a page in use cannot be evicted (its frame pointer is live);
//   - dirty:     a modified page must be written back to disk before reuse.
class BufferPool {
public:
    explicit BufferPool(DiskManager& disk, std::size_t num_frames = DEFAULT_POOL_FRAMES);

    // Flush all dirty frames on destruction so a clean shutdown is durable
    // without the caller having to remember flush_all(). Safe because the pool
    // is declared before — so destroyed after — the DiskManager it writes to.
    ~BufferPool();

    // Bring page `id` into a frame (loading from disk on a miss) and pin it.
    // Returns a pointer to its PAGE_SIZE bytes, valid until the matching unpin.
    char* fetch_page(PageID id);

    // Release one pin on `id`. Pass dirty=true if you modified the page.
    bool unpin_page(PageID id, bool dirty);

    // Grow the data file by one page and return its id (not yet pinned).
    PageID new_page();

    void flush_page(PageID id);
    void flush_all();  // write every dirty frame back to disk

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

    std::size_t find_victim();  // pick a frame to (re)use; throws if all pinned
};
