#pragma once
#include <unordered_map>
#include <mutex>
#include "storage/page.h"
#include "storage/disk_manager.h"
#include "storage/lru_replacer.h"

namespace minidb {

// The BufferPoolManager caches a fixed number of disk pages in memory and is
// the single access point for the rest of the engine: every read or write of a
// page goes through fetch_page / new_page. It tracks pin counts, dirty flags,
// and uses an LRUReplacer to choose eviction victims.
class BufferPoolManager {
public:
    BufferPoolManager(size_t pool_size, DiskManager *disk);
    ~BufferPoolManager();

    // Allocate a brand new page on disk, pin it, and return the in-memory frame.
    // *page_id receives the new id. Returns nullptr if the pool is full of
    // pinned pages.
    Page *new_page(page_id_t *page_id);

    // Fetch the page with `page_id`, loading it from disk if not cached. The
    // returned page is pinned; the caller must unpin_page when done.
    Page *fetch_page(page_id_t page_id);

    // Release a previously fetched page. `is_dirty` records whether the caller
    // modified it. When pin count reaches zero the frame becomes evictable.
    bool unpin_page(page_id_t page_id, bool is_dirty);

    // Force a single page to disk (regardless of pin count).
    bool flush_page(page_id_t page_id);

    // Force every dirty page to disk (used at shutdown / checkpoint).
    void flush_all();

    DiskManager *disk_manager() { return disk_; }

private:
    // Find a free frame, or evict an LRU victim (writing it back if dirty).
    // Returns false if no frame can be obtained.
    bool find_victim_frame(frame_id_t *frame_id);

    size_t                                    pool_size_;
    Page                                     *frames_;       // array of frames
    DiskManager                              *disk_;
    LRUReplacer                               replacer_;
    std::unordered_map<page_id_t, frame_id_t> page_table_;   // page_id -> frame
    std::list<frame_id_t>                     free_list_;     // unused frames
    std::mutex                                latch_;
};

} // namespace minidb
