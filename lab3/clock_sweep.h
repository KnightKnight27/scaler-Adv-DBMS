#pragma once
/**
 * Lab 3 — ClockSweep Buffer Pool Replacement Algorithm
 *
 * Implements PostgreSQL's eviction strategy for the shared buffer pool.
 * The ClockSweep algorithm is a variant of the Clock (Second-Chance) algorithm,
 * which approximates LRU with O(1) eviction decisions.
 */

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <iostream>
#include <cassert>
#include <stdexcept>
#include <iomanip>

// ─────────────────────────────────────────────────
// Page: Represents a disk page (simplified)
// ─────────────────────────────────────────────────
struct Page {
    uint32_t page_id;
    char     data[64];  // simplified: real pages are 8KB (PostgreSQL) or 4KB

    Page() : page_id(0) {
        std::memset(data, 0, sizeof(data));
    }

    explicit Page(uint32_t id) : page_id(id) {
        std::snprintf(data, sizeof(data), "Page_%u_data", id);
    }
};

// ─────────────────────────────────────────────────
// BufferFrame: A slot in the buffer pool
// ─────────────────────────────────────────────────
struct BufferFrame {
    Page     page;
    bool     valid;         // is this frame occupied?
    bool     dirty;         // has the page been modified?
    uint32_t pin_count;     // number of active users (pinned = cannot evict)
    uint32_t usage_count;   // access counter for ClockSweep (decremented on sweep)

    BufferFrame()
        : valid(false), dirty(false), pin_count(0), usage_count(0) {}

    void reset() {
        valid = false;
        dirty = false;
        pin_count = 0;
        usage_count = 0;
        page = Page();
    }
};

// ─────────────────────────────────────────────────
// DiskManager: Simulates reading/writing pages from/to disk
// ─────────────────────────────────────────────────
class DiskManager {
public:
    uint32_t reads  = 0;
    uint32_t writes = 0;

    Page read_page(uint32_t page_id) {
        reads++;
        // In a real system, this would do: lseek() + read() on the data file
        return Page(page_id);
    }

    void write_page(const Page& page) {
        writes++;
        // In a real system, this would do: lseek() + write() + optional fsync()
        (void)page;  // suppress unused warning
    }

    void reset_stats() {
        reads = 0;
        writes = 0;
    }
};

// ─────────────────────────────────────────────────
// ClockSweepBufferPool: The main buffer pool manager
// ─────────────────────────────────────────────────
class ClockSweepBufferPool {
private:
    std::vector<BufferFrame>                  frames_;
    std::unordered_map<uint32_t, uint32_t>    page_table_;  // page_id → frame_index
    uint32_t                                  pool_size_;
    uint32_t                                  clock_hand_;  // current position of the clock hand
    DiskManager&                              disk_;

    // Statistics
    uint32_t hit_count_  = 0;
    uint32_t miss_count_ = 0;
    uint32_t eviction_count_ = 0;

    /**
     * ClockSweep Eviction Algorithm
     *
     * PostgreSQL's buffer replacement strategy:
     * 1. Start from the clock hand position
     * 2. For each frame examined:
     *    a. If pin_count > 0: skip (page is in use)
     *    b. If usage_count > 0: decrement usage_count, advance hand
     *    c. If usage_count == 0 and pin_count == 0: EVICT this frame
     * 3. If we go around the entire buffer without finding a victim,
     *    it means all pages are pinned — throw an error.
     *
     * Why ClockSweep over LRU?
     * - LRU requires maintaining a doubly-linked list (expensive under contention)
     * - ClockSweep only needs an atomic decrement — much better for concurrent access
     * - Approximates LRU behavior: frequently accessed pages have higher usage_count
     */
    uint32_t find_victim() {
        uint32_t start = clock_hand_;
        uint32_t scanned = 0;

        while (true) {
            BufferFrame& frame = frames_[clock_hand_];

            if (!frame.valid) {
                // Empty frame — use it immediately
                uint32_t victim = clock_hand_;
                advance_clock();
                return victim;
            }

            if (frame.pin_count == 0) {
                if (frame.usage_count == 0) {
                    // Found a victim: unpinned with usage_count = 0
                    uint32_t victim = clock_hand_;
                    advance_clock();
                    eviction_count_++;
                    return victim;
                } else {
                    // Give it a second chance: decrement usage_count
                    frame.usage_count--;
                }
            }
            // If pinned, skip entirely

            advance_clock();
            scanned++;

            if (scanned >= pool_size_ * 2) {
                // Safety check: went around twice without finding a victim
                throw std::runtime_error(
                    "BufferPool: All pages are pinned! Cannot evict. "
                    "This is a classic buffer pool exhaustion scenario."
                );
            }
        }
    }

    void advance_clock() {
        clock_hand_ = (clock_hand_ + 1) % pool_size_;
    }

public:
    ClockSweepBufferPool(uint32_t pool_size, DiskManager& disk)
        : frames_(pool_size), pool_size_(pool_size), clock_hand_(0), disk_(disk)
    {
        assert(pool_size > 0);
    }

    /**
     * FetchPage — Bring a page into the buffer pool
     *
     * 1. Check page_table_ for the page (buffer pool hit)
     * 2. If miss: find a victim frame via ClockSweep
     * 3. If victim is dirty: flush it to disk first
     * 4. Read the requested page from disk into the frame
     * 5. Pin the page (increment pin_count)
     * 6. Set usage_count (like PostgreSQL's BM_MAX_USAGE_COUNT)
     *
     * Returns: pointer to the page data
     */
    Page* fetch_page(uint32_t page_id) {
        // Check for buffer pool hit
        auto it = page_table_.find(page_id);
        if (it != page_table_.end()) {
            BufferFrame& frame = frames_[it->second];
            frame.pin_count++;
            // Increment usage_count (capped at 5 like PostgreSQL's BM_MAX_USAGE_COUNT)
            if (frame.usage_count < 5) {
                frame.usage_count++;
            }
            hit_count_++;
            return &frame.page;
        }

        // Buffer pool miss — need to bring the page in
        miss_count_++;

        // Find a victim frame
        uint32_t frame_idx = find_victim();
        BufferFrame& frame = frames_[frame_idx];

        // If victim frame has a valid dirty page, flush it to disk
        if (frame.valid && frame.dirty) {
            disk_.write_page(frame.page);
        }

        // Remove old page from page table
        if (frame.valid) {
            page_table_.erase(frame.page.page_id);
        }

        // Read new page from disk
        frame.page = disk_.read_page(page_id);
        frame.valid = true;
        frame.dirty = false;
        frame.pin_count = 1;
        frame.usage_count = 1;  // fresh page gets usage_count = 1

        // Register in page table
        page_table_[page_id] = frame_idx;

        return &frame.page;
    }

    /**
     * UnpinPage — Release a pin on a page
     *
     * After a backend is done with a page, it unpins it.
     * The page stays in the buffer pool but becomes eligible for eviction
     * once pin_count reaches 0.
     */
    bool unpin_page(uint32_t page_id, bool is_dirty = false) {
        auto it = page_table_.find(page_id);
        if (it == page_table_.end()) {
            return false;  // page not in buffer pool
        }

        BufferFrame& frame = frames_[it->second];
        if (frame.pin_count == 0) {
            return false;  // already unpinned
        }

        frame.pin_count--;
        if (is_dirty) {
            frame.dirty = true;
        }
        return true;
    }

    /**
     * FlushPage — Write a specific page to disk
     */
    bool flush_page(uint32_t page_id) {
        auto it = page_table_.find(page_id);
        if (it == page_table_.end()) {
            return false;
        }

        BufferFrame& frame = frames_[it->second];
        if (frame.dirty) {
            disk_.write_page(frame.page);
            frame.dirty = false;
        }
        return true;
    }

    /**
     * FlushAll — Flush all dirty pages (like a checkpoint)
     */
    void flush_all() {
        for (auto& frame : frames_) {
            if (frame.valid && frame.dirty) {
                disk_.write_page(frame.page);
                frame.dirty = false;
            }
        }
    }

    // ─────────────────────────────────────────────────
    // Display / Debug utilities
    // ─────────────────────────────────────────────────
    void print_state() const {
        std::cout << "\n┌─────────────────────────────────────────────────────────────────┐" << std::endl;
        std::cout << "│  Buffer Pool State  (pool_size=" << pool_size_
                  << ", clock_hand=" << clock_hand_ << ")" << std::endl;
        std::cout << "├─────┬──────────┬───────┬───────┬─────────┬────────────────────────┤" << std::endl;
        std::cout << "│ Idx │ Page ID  │ Pin   │ Usage │ Dirty   │ Clock                  │" << std::endl;
        std::cout << "├─────┼──────────┼───────┼───────┼─────────┼────────────────────────┤" << std::endl;

        for (uint32_t i = 0; i < pool_size_; ++i) {
            const BufferFrame& f = frames_[i];
            std::string clock_marker = (i == clock_hand_) ? " ◄── clock hand" : "";

            if (f.valid) {
                std::cout << "│ " << std::setw(3) << i << " │ "
                          << std::setw(8) << f.page.page_id << " │ "
                          << std::setw(5) << f.pin_count << " │ "
                          << std::setw(5) << f.usage_count << " │ "
                          << std::setw(7) << (f.dirty ? "YES" : "no") << " │"
                          << std::setw(23) << clock_marker << " │" << std::endl;
            } else {
                std::cout << "│ " << std::setw(3) << i << " │ "
                          << std::setw(8) << "(empty)" << " │ "
                          << std::setw(5) << "-" << " │ "
                          << std::setw(5) << "-" << " │ "
                          << std::setw(7) << "-" << " │"
                          << std::setw(23) << clock_marker << " │" << std::endl;
            }
        }
        std::cout << "└─────┴──────────┴───────┴───────┴─────────┴────────────────────────┘" << std::endl;
    }

    void print_stats() const {
        uint32_t total = hit_count_ + miss_count_;
        double hit_rate = total > 0 ? (100.0 * hit_count_ / total) : 0.0;

        std::cout << "\n  Buffer Pool Statistics:" << std::endl;
        std::cout << "  ├── Hits:       " << hit_count_ << std::endl;
        std::cout << "  ├── Misses:     " << miss_count_ << std::endl;
        std::cout << "  ├── Hit Rate:   " << std::fixed << std::setprecision(1)
                  << hit_rate << "%" << std::endl;
        std::cout << "  ├── Evictions:  " << eviction_count_ << std::endl;
        std::cout << "  ├── Disk Reads: " << disk_.reads << std::endl;
        std::cout << "  └── Disk Writes:" << disk_.writes << std::endl;
    }

    void reset_stats() {
        hit_count_ = 0;
        miss_count_ = 0;
        eviction_count_ = 0;
        disk_.reset_stats();
    }

    uint32_t pool_size() const { return pool_size_; }
    uint32_t hit_count() const { return hit_count_; }
    uint32_t miss_count() const { return miss_count_; }
    uint32_t eviction_count() const { return eviction_count_; }
};
