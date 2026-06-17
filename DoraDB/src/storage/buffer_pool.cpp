#include "storage/buffer_pool.h"
#include <stdexcept>
#include <iostream>

BufferPool::BufferPool(DiskManager* disk_mgr, int pool_size)
    : disk_mgr_(disk_mgr),
      frames_(pool_size),
      frame_info_(pool_size),
      pool_size_(pool_size) {}

BufferPool::~BufferPool() {
    FlushAll();
}

// ============================================================
// FetchPage — bring a page into the pool (or find it already there)
// ============================================================

Page* BufferPool::FetchPage(int page_id) {
    // 1. Check if page is already in the pool (cache hit)
    auto it = page_table_.find(page_id);
    if (it != page_table_.end()) {
        int frame_id = it->second;
        frame_info_[frame_id].pin_count++;
        frame_info_[frame_id].ref_bit = true;  // second chance
        return &frames_[frame_id];
    }

    // 2. Cache miss — find a frame to use
    int frame_id = FindVictimFrame();
    if (frame_id == -1) {
        throw std::runtime_error("BufferPool: all frames pinned, cannot fetch page");
    }

    // 3. If the victim frame had a page, evict it
    if (frame_info_[frame_id].page_id != INVALID_PAGE_ID) {
        // Flush if dirty
        if (frame_info_[frame_id].dirty) {
            disk_mgr_->WritePage(frame_info_[frame_id].page_id,
                                 frames_[frame_id].GetData());
        }
        // Remove from page table
        page_table_.erase(frame_info_[frame_id].page_id);
    }

    // 4. Read the requested page from disk into this frame
    disk_mgr_->ReadPage(page_id, frames_[frame_id].GetData());

    // 5. Update metadata
    frame_info_[frame_id].page_id = page_id;
    frame_info_[frame_id].pin_count = 1;
    frame_info_[frame_id].dirty = false;
    frame_info_[frame_id].ref_bit = true;
    page_table_[page_id] = frame_id;

    return &frames_[frame_id];
}

// ============================================================
// UnpinPage — done using this page, let it be evictable
// ============================================================

void BufferPool::UnpinPage(int page_id, bool is_dirty) {
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) return;  // not in pool, nothing to do

    int frame_id = it->second;
    if (frame_info_[frame_id].pin_count > 0) {
        frame_info_[frame_id].pin_count--;
    }
    if (is_dirty) {
        frame_info_[frame_id].dirty = true;
    }
}

// ============================================================
// FlushPage — write dirty page to disk
// ============================================================

void BufferPool::FlushPage(int page_id) {
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) return;

    int frame_id = it->second;
    if (frame_info_[frame_id].dirty) {
        disk_mgr_->WritePage(page_id, frames_[frame_id].GetData());
        frame_info_[frame_id].dirty = false;
    }
}

// ============================================================
// NewPage — allocate a fresh page on disk and bring it into pool
// ============================================================

Page* BufferPool::NewPage(int& page_id) {
    // Allocate on disk
    page_id = disk_mgr_->AllocatePage();

    // Find a frame
    int frame_id = FindVictimFrame();
    if (frame_id == -1) {
        throw std::runtime_error("BufferPool: all frames pinned, cannot allocate new page");
    }

    // Evict old page from this frame if needed
    if (frame_info_[frame_id].page_id != INVALID_PAGE_ID) {
        if (frame_info_[frame_id].dirty) {
            disk_mgr_->WritePage(frame_info_[frame_id].page_id,
                                 frames_[frame_id].GetData());
        }
        page_table_.erase(frame_info_[frame_id].page_id);
    }

    // Initialize the new page in the frame
    frames_[frame_id].Init(page_id);

    // Update metadata
    frame_info_[frame_id].page_id = page_id;
    frame_info_[frame_id].pin_count = 1;
    frame_info_[frame_id].dirty = true;  // new page needs to be written
    frame_info_[frame_id].ref_bit = true;
    page_table_[page_id] = frame_id;

    return &frames_[frame_id];
}

// ============================================================
// FlushAll — write every dirty page to disk
// ============================================================

void BufferPool::FlushAll() {
    for (int i = 0; i < pool_size_; i++) {
        if (frame_info_[i].page_id != INVALID_PAGE_ID && frame_info_[i].dirty) {
            disk_mgr_->WritePage(frame_info_[i].page_id, frames_[i].GetData());
            frame_info_[i].dirty = false;
        }
    }
}

// ============================================================
// FindVictimFrame — Clock Sweep algorithm
//
// Same concept as Lab03's accessPage, cleaned up:
//   1. Look for an empty frame first
//   2. Otherwise sweep: if ref_bit=1 → clear it, advance.
//      If ref_bit=0 and pin_count=0 → victim found.
//   3. If we go around the entire clock twice without finding
//      a victim, all frames are pinned → return -1.
// ============================================================

int BufferPool::FindVictimFrame() {
    // First pass: look for an empty frame (never been used)
    for (int i = 0; i < pool_size_; i++) {
        if (frame_info_[i].page_id == INVALID_PAGE_ID) {
            return i;
        }
    }

    // Clock sweep: try at most 2 full rotations
    int max_attempts = pool_size_ * 2;
    for (int attempt = 0; attempt < max_attempts; attempt++) {
        FrameInfo& info = frame_info_[clock_hand_];

        if (info.pin_count == 0) {
            if (info.ref_bit) {
                // Second chance: clear the bit and move on
                info.ref_bit = false;
            } else {
                // Victim found!
                int victim = clock_hand_;
                clock_hand_ = (clock_hand_ + 1) % pool_size_;
                return victim;
            }
        }
        // Skip pinned frames and advance the hand
        clock_hand_ = (clock_hand_ + 1) % pool_size_;
    }

    return -1;  // all frames pinned
}
