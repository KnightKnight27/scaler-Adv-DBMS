#ifndef MINIDB_BUFFER_POOL_H
#define MINIDB_BUFFER_POOL_H

#include <list>
#include <unordered_map>

#include "DiskManager.h"
#include "Page.h"

/**
 * BufferPool caches pages in memory using an LRU eviction policy.
 *
 * Uses std::unordered_map for O(1) page lookup and std::list for
 * O(1) LRU ordering. Pages are pinned while in use and cannot be
 * evicted until unpinned.
 *
 * Memory is managed explicitly — Page objects are allocated with new
 * and deallocated with delete.
 */
class BufferPool {
public:
    static constexpr int DEFAULT_CAPACITY = 50;

    BufferPool(DiskManager* diskManager, int capacity);
    explicit BufferPool(DiskManager* diskManager);
    ~BufferPool();

    // Non-copyable, non-movable
    BufferPool(const BufferPool&) = delete;
    BufferPool& operator=(const BufferPool&) = delete;

    /**
     * Fetches the page with the given pageId into the buffer pool.
     * If already cached, promotes it in the LRU list.
     * Pins the page (increments pin count).
     *
     * Returns a raw, non-owning pointer to the Page.
     */
    Page* fetchPage(int pageId);

    /**
     * Unpins the page. Must be called when done reading/writing.
     * If isDirty is true, marks the page as dirty.
     */
    void unpinPage(int pageId, bool isDirty);

    /** Writes all dirty pages to disk. */
    void flushAllPages();

    DiskManager* getDiskManager() { return diskManager_; }

private:
    void evictPage();
    void promoteLRU(int pageId);

    DiskManager* diskManager_;  // non-owning
    int capacity_;

    // pageId → heap-allocated Page*
    std::unordered_map<int, Page*> pageTable_;

    // LRU list: front = most recently used, back = least recently used
    std::list<int> lruList_;

    // pageId → iterator into lruList_ for O(1) removal/promotion
    std::unordered_map<int, std::list<int>::iterator> lruMap_;

    // pageId → pin reference count
    std::unordered_map<int, int> pinCounts_;
};

#endif // MINIDB_BUFFER_POOL_H
