#pragma once

#include "storage.h"

#include <cstring>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>

// A single frame in the buffer pool.
struct Page {
    char data[PAGE_SIZE]{};
    page_id_t page_id{INVALID_PAGE_ID};
    int pin_count{0};
    bool is_dirty{false};

    void Reset() {
        std::memset(data, 0, PAGE_SIZE);
        page_id = INVALID_PAGE_ID;
        pin_count = 0;
        is_dirty = false;
    }
};

class BufferPoolManager {
public:
    BufferPoolManager(size_t pool_size, DiskManager *disk_manager);
    ~BufferPoolManager();

    // Allocate a new page on disk and pin it in the pool.
    // Returns nullptr if no frame is available.
    Page *NewPage(page_id_t &page_id);

    // Fetch an existing page from disk (or the pool if already buffered).
    // Returns nullptr if no frame is available.
    Page *FetchPage(page_id_t page_id);

    // Decrement the pin count of a page. Mark dirty if the caller modified it.
    // Returns false if the page is not in the pool.
    bool UnpinPage(page_id_t page_id, bool is_dirty);

    // Write a dirty page to disk immediately.
    bool FlushPage(page_id_t page_id);

    // Flush every dirty page in the pool.
    void FlushAllPages();

    // Remove a page from the pool (must be unpinned).
    bool DeletePage(page_id_t page_id);

    size_t PoolSize() const { return pool_size_; }

    // Returns true when every buffered page has pin_count == 0.
    bool AllUnpinned() const;

private:
    size_t pool_size_;
    DiskManager *disk_manager_;

    // The actual frames.
    std::unique_ptr<Page[]> pages_;

    // Free frame indices.
    std::list<size_t> free_list_;

    // page_id -> frame index for buffered pages.
    std::unordered_map<page_id_t, size_t> page_table_;

    // LRU list: front = most recently used frame index, back = LRU.
    std::list<size_t> lru_list_;
    // frame index -> iterator into lru_list_ for O(1) removal.
    std::unordered_map<size_t, std::list<size_t>::iterator> lru_map_;

    // Record a frame access (moves it to the front of lru_list_).
    void TouchFrame(size_t frame_id);

    // Find a victim frame using LRU. Returns pool_size_ on failure.
    size_t Evict();
};
