#include "storage/buffer_pool.h"
#include <iostream>
#include <stdexcept>

// ─── Constructor / Destructor ─────────────────────────────────────────────────

BufferPool::BufferPool(DiskManager& disk)
    : disk_(disk)
{
    // Pre-allocate all frames upfront so we never reallocate
    frames_.resize(BUFFER_POOL_SIZE);
}

BufferPool::~BufferPool() {
    // On shutdown, flush all dirty pages so nothing is lost
    flushAll();
}

// ─── pinPage ──────────────────────────────────────────────────────────────────
//
// Brings page_id into a buffer frame and pins it.
// If it's already in the pool, just bump the pin count and set ref_bit.
// Otherwise, find a free/evictable frame and load from disk.

Page* BufferPool::pinPage(PageID page_id) {
    // Fast path: page is already in the pool
    auto it = page_table_.find(page_id);
    if (it != page_table_.end()) {
        Frame& f = frames_[it->second];
        f.pin_count++;
        f.ref_bit = true;  // mark as recently used
        return &f.page;
    }

    // Slow path: need to load from disk — first find a frame to use
    int idx = clockSweep();
    if (idx < 0) {
        // All frames are pinned; pool is exhausted
        std::cerr << "[BufferPool] ERROR: all frames pinned, pool exhausted\n";
        return nullptr;
    }

    // Evict whatever was in this frame
    evictFrame(idx);

    // Load the requested page from disk
    Frame& frame = frames_[idx];
    if (!disk_.readPage(page_id, frame.page)) {
        std::cerr << "[BufferPool] ERROR: could not read page " << page_id << " from disk\n";
        return nullptr;
    }

    // Set up the frame metadata
    frame.page_id   = page_id;
    frame.pin_count = 1;
    frame.dirty     = false;
    frame.ref_bit   = true;
    frame.valid     = true;

    // Register in the page table
    page_table_[page_id] = idx;

    return &frame.page;
}

// ─── unpinPage ────────────────────────────────────────────────────────────────

void BufferPool::unpinPage(PageID page_id, bool dirty) {
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) {
        return;  // page not in pool — nothing to do
    }

    Frame& f = frames_[it->second];
    if (f.pin_count > 0) {
        f.pin_count--;
    }

    // Once dirty, always dirty until written back
    if (dirty) {
        f.dirty = true;
    }
}

// ─── flushPage ────────────────────────────────────────────────────────────────

void BufferPool::flushPage(PageID page_id) {
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) {
        return;  // not in pool
    }

    Frame& f = frames_[it->second];
    if (f.dirty) {
        disk_.writePage(page_id, f.page);
        f.dirty = false;
    }
}

// ─── flushAll ─────────────────────────────────────────────────────────────────

void BufferPool::flushAll() {
    for (auto& frame : frames_) {
        if (frame.valid && frame.dirty) {
            disk_.writePage(frame.page_id, frame.page);
            frame.dirty = false;
        }
    }
}

// ─── newPage ──────────────────────────────────────────────────────────────────

Page* BufferPool::newPage(PageID& new_page_id) {
    // Allocate a new page on disk
    new_page_id = disk_.allocatePage();
    if (new_page_id == INVALID_PAGE_ID) {
        return nullptr;
    }

    // Bring it into the buffer pool (it was just written by allocatePage,
    // so pinPage will load it back — that's fine for correctness)
    return pinPage(new_page_id);
}

// ─── clockSweep ───────────────────────────────────────────────────────────────
//
// Clock-Sweep eviction (same algorithm as Lab 3):
//   - Rotate the clock hand around the frames
//   - If a frame is invalid (empty)  → use it immediately
//   - If pinned (pin_count > 0)      → skip
//   - If ref_bit is true             → clear it, move on (second chance)
//   - If ref_bit is false            → evict this frame
//
// Returns the index of the chosen frame, or -1 if all are pinned.

int BufferPool::clockSweep() {
    int full_rotations = 0;
    int n = static_cast<int>(frames_.size());

    while (full_rotations < 2) {
        Frame& f = frames_[clock_hand_];

        if (!f.valid) {
            // Empty frame — grab it immediately
            int chosen = clock_hand_;
            clock_hand_ = (clock_hand_ + 1) % n;
            return chosen;
        }

        if (f.pin_count == 0) {
            if (!f.ref_bit) {
                // Candidate for eviction
                int chosen = clock_hand_;
                clock_hand_ = (clock_hand_ + 1) % n;
                return chosen;
            } else {
                // Give it a second chance
                f.ref_bit = false;
            }
        }

        clock_hand_ = (clock_hand_ + 1) % n;

        // If we've gone all the way around, count a rotation
        if (clock_hand_ == 0) {
            full_rotations++;
        }
    }

    return -1;  // all frames pinned
}

// ─── evictFrame ───────────────────────────────────────────────────────────────
// Write back if dirty, then clear the frame so it can be reused.

void BufferPool::evictFrame(int frame_idx) {
    Frame& f = frames_[frame_idx];

    if (!f.valid) {
        return;  // already empty
    }

    if (f.dirty) {
        // Must write back before evicting (Write-Ahead principle for the buffer pool)
        disk_.writePage(f.page_id, f.page);
        f.dirty = false;
    }

    // Remove from page table
    page_table_.erase(f.page_id);

    // Reset frame metadata
    f.page_id   = INVALID_PAGE_ID;
    f.pin_count = 0;
    f.ref_bit   = false;
    f.valid     = false;
}
