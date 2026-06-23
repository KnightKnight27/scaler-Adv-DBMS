// Buffer pool: a fixed set of in-memory frames caching disk pages.
//
//   * Keeps at most `pool_size` pages resident.
//   * Tracks dirty pages and writes them back on eviction / flush.
//   * Pins/unpins pages so an in-use page is never evicted.
//   * Evicts the least-recently-used *unpinned* frame.
//
// The `before_flush` hook lets the recovery manager enforce the write-ahead
// rule: a page's log records must reach disk before the page itself.
#pragma once

#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "storage/disk_manager.hpp"
#include "storage/page.hpp"

namespace minidb {

class BufferPool {
public:
    BufferPool(DiskManager* disk, size_t pool_size = 128)
        : disk_(disk), pool_size_(pool_size) {}

    // Returns a pinned page; caller must unpin_page() when done.
    Page* fetch_page(PageId pid);
    // Allocates a new on-disk page and returns it pinned + dirty.
    Page* new_page();

    void unpin_page(PageId pid, bool is_dirty);
    void flush_page(PageId pid);
    void flush_all();

    void set_before_flush(std::function<void(PageId)> cb) { before_flush_ = std::move(cb); }

    struct Stats { size_t hits = 0, misses = 0, evictions = 0; };
    Stats stats() const { return stats_; }

private:
    struct Frame {
        Page page;
        int  pin_count = 0;
        bool dirty = false;
        std::list<PageId>::iterator lru_it;
    };

    void evict_if_needed();          // ensure room for one more frame
    void flush_frame(Frame& f);
    void touch(PageId pid);          // mark most-recently-used

    DiskManager* disk_;
    size_t pool_size_;
    std::unordered_map<PageId, std::unique_ptr<Frame>> frames_;
    std::list<PageId> lru_;          // front = LRU, back = MRU
    std::recursive_mutex mtx_;
    std::function<void(PageId)> before_flush_;
    Stats stats_;
};

}  // namespace minidb
