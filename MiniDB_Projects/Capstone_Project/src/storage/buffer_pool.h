#pragma once

#include "storage/disk_manager.h"
#include "storage/page.h"
#include "common/types.h"
#include "common/config.h"
#include <unordered_map>
#include <vector>
#include <cstdint>

/**
 * @class BufferPool
 * @brief Coordinates in-memory caching of disk pages.
 *
 * Employs a Clock-Sweep page replacement policy to manage a fixed cache pool of page frames.
 * Key properties:
 * - Pinned pages (pin_count > 0) are pinned to memory and cannot be evicted.
 * - Dirty pages are automatically persisted to disk before eviction or upon commit.
 */
class BufferPool {
public:
    /**
     * @brief Construct a new Buffer Pool Manager.
     * @param disk The underlying DiskManager to execute physical page reads/writes.
     */
    explicit BufferPool(DiskManager& disk);

    /**
     * @brief Destructor. Automatically flushes all dirty pages to persist changes.
     */
    ~BufferPool();

    // Disable copy constructors to avoid cache duplication
    BufferPool(const BufferPool&) = delete;
    BufferPool& operator=(const BufferPool&) = delete;

    /**
     * @brief Fetches a page by its PageID, pins it in memory, and returns a pointer to it.
     * Brings the page from disk if it is not already present in the pool.
     * Caller MUST release the page using unpinPage().
     *
     * @param page_id The PageID of the page to pin.
     * @return Pointer to the Page, or nullptr if the pool is exhausted.
     */
    Page* pinPage(PageID page_id);

    /**
     * @brief Releases a pin on a page.
     * @param page_id The PageID to release.
     * @param dirty Set to true if page data was modified while pinned.
     */
    void unpinPage(PageID page_id, bool dirty = false);

    /**
     * @brief Persists a specific dirty page back to disk immediately.
     * @param page_id The PageID to flush.
     */
    void flushPage(PageID page_id);

    /**
     * @brief Flushes all dirty pages currently residing in the buffer pool to disk.
     */
    void flushAll();

    /**
     * @brief Allocates a new physical page on disk and loads it as pinned into the pool.
     * @param[out] new_page_id Outputs the PageID of the allocated page.
     * @return Pointer to the allocated Page, or nullptr on failure.
     */
    Page* newPage(PageID& new_page_id);

private:
    /**
     * @struct Frame
     * @brief Tracks metadata for one slot inside the in-memory cache pool.
     */
    struct Frame {
        Page page;                      ///< In-memory page contents
        PageID page_id = INVALID_PAGE_ID; ///< Loaded Page ID
        int pin_count = 0;              ///< Readers/writers accessing this page frame
        bool dirty = false;             ///< Does this page contain unwritten updates?
        bool ref_bit = false;           ///< Clock-sweep usage marker
        bool valid = false;             ///< Does this frame contain a real page?
    };

    /**
     * @brief Runs the clock-sweep eviction algorithm to locate a frame for replacement.
     * @return Index of the selected frame, or -1 if all frames are pinned.
     */
    int clockSweep();

    /**
     * @brief Persists the page in the specified frame if dirty, and clears frame metadata.
     * @param frame_idx Index of the frame to evict.
     */
    void evictFrame(int frame_idx);

    DiskManager& disk_;                     ///< Underlying disk controller reference
    std::vector<Frame> frames_;             ///< Fixed array of cache frames
    std::unordered_map<PageID, int> page_table_; ///< Maps active PageIDs to frame indexes
    int clock_hand_ = 0;                    ///< Circular sweep index pointer
};
