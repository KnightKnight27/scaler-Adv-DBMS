#include "storage/buffer_pool.h"
#include <iostream>
#include <stdexcept>

BufferPool::BufferPool(DiskManager& disk)
    : disk_(disk) {
    frames_.resize(BUFFER_POOL_SIZE);
}

BufferPool::~BufferPool() {
    flushAll();
}

Page* BufferPool::pinPage(PageID page_id) {
    // Check if the page is already cached in memory
    const auto it = page_table_.find(page_id);
    if (it != page_table_.end()) {
        Frame& frame = frames_[it->second];
        frame.pin_count++;
        frame.ref_bit = true;
        return &frame.page;
    }

    // Identify a candidate frame for replacement via Clock-Sweep
    const int frame_idx = clockSweep();
    if (frame_idx < 0) {
        std::cerr << "[BufferPool] ERROR: All frames pinned. Buffer pool exhausted.\n";
        return nullptr;
    }

    evictFrame(frame_idx);

    Frame& frame = frames_[frame_idx];
    if (!disk_.readPage(page_id, frame.page)) {
        std::cerr << "[BufferPool] ERROR: Failed to read page " << page_id << " from disk.\n";
        return nullptr;
    }

    // Populate frame control metadata
    frame.page_id = page_id;
    frame.pin_count = 1;
    frame.dirty = false;
    frame.ref_bit = true;
    frame.valid = true;

    page_table_[page_id] = frame_idx;
    return &frame.page;
}

void BufferPool::unpinPage(PageID page_id, bool dirty) {
    const auto it = page_table_.find(page_id);
    if (it == page_table_.end()) {
        return;
    }

    Frame& frame = frames_[it->second];
    if (frame.pin_count > 0) {
        frame.pin_count--;
    }

    if (dirty) {
        frame.dirty = true;
    }
}

void BufferPool::flushPage(PageID page_id) {
    const auto it = page_table_.find(page_id);
    if (it == page_table_.end()) {
        return;
    }

    Frame& frame = frames_[it->second];
    if (frame.dirty) {
        disk_.writePage(page_id, frame.page);
        frame.dirty = false;
    }
}

void BufferPool::flushAll() {
    for (auto& frame : frames_) {
        if (frame.valid && frame.dirty) {
            disk_.writePage(frame.page_id, frame.page);
            frame.dirty = false;
        }
    }
}

Page* BufferPool::newPage(PageID& new_page_id) {
    new_page_id = disk_.allocatePage();
    if (new_page_id == INVALID_PAGE_ID) {
        return nullptr;
    }
    return pinPage(new_page_id);
}

int BufferPool::clockSweep() {
    int full_rotations = 0;
    const int frame_count = static_cast<int>(frames_.size());

    while (full_rotations < 2) {
        Frame& frame = frames_[clock_hand_];

        // Empty slot is immediately usable
        if (!frame.valid) {
            const int chosen_idx = clock_hand_;
            clock_hand_ = (clock_hand_ + 1) % frame_count;
            return chosen_idx;
        }

        // Unpinned frames are candidates for clock sweeps
        if (frame.pin_count == 0) {
            if (!frame.ref_bit) {
                const int chosen_idx = clock_hand_;
                clock_hand_ = (clock_hand_ + 1) % frame_count;
                return chosen_idx;
            }
            // Give second chance
            frame.ref_bit = false;
        }

        clock_hand_ = (clock_hand_ + 1) % frame_count;
        if (clock_hand_ == 0) {
            full_rotations++;
        }
    }

    return -1;
}

void BufferPool::evictFrame(int frame_idx) {
    Frame& frame = frames_[frame_idx];
    if (!frame.valid) {
        return;
    }

    if (frame.dirty) {
        disk_.writePage(frame.page_id, frame.page);
        frame.dirty = false;
    }

    page_table_.erase(frame.page_id);

    // Reset frame state variables
    frame.page_id = INVALID_PAGE_ID;
    frame.pin_count = 0;
    frame.ref_bit = false;
    frame.valid = false;
}
