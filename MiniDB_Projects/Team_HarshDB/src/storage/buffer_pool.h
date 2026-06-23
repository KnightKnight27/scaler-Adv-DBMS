#pragma once
// ---------------------------------------------------------------------------
// buffer_pool.h - the in-memory cache of disk pages with ClockSweep eviction.
//
// This is the integrated version of Lab 3. The buffer pool holds a fixed number
// of frames. fetch_page() returns a pinned page (loading it from disk on a
// miss); callers unpin when done. When every frame is occupied we pick a victim
// using the ClockSweep ("second chance") algorithm PostgreSQL uses instead of
// strict LRU: a circular hand sweeps frames, giving each a chance by decrementing
// its usage_count, and evicts the first unpinned frame whose count hits zero.
// ---------------------------------------------------------------------------
#include "page.h"
#include "disk_manager.h"
#include <unordered_map>
#include <vector>
#include <mutex>
#include <stdexcept>

namespace minidb {

class BufferPool {
public:
    struct Frame {
        Page page;
        int  page_id     = INVALID_PAGE_ID;
        bool dirty       = false;
        int  pin_count   = 0;
        int  usage_count = 0;
    };

    BufferPool(DiskManager* dm, int capacity)
        : dm_(dm), capacity_(capacity), frames_(capacity) {}

    // Pin and return the page with this id, loading it from disk on a miss.
    Page* fetch_page(int page_id) {
        std::lock_guard<std::recursive_mutex> lk(mu_);
        auto it = table_.find(page_id);
        if (it != table_.end()) {
            Frame& f = frames_[it->second];
            f.pin_count++;
            f.usage_count = std::min(f.usage_count + 1, 5);
            return &f.page;
        }
        int fid = pick_frame();
        Frame& f = frames_[fid];
        dm_->read_page(page_id, f.page.data);
        f.page_id = page_id;
        f.dirty = false;
        f.pin_count = 1;
        f.usage_count = 1;
        table_[page_id] = fid;
        return &f.page;
    }

    // Allocate a brand new page on disk, initialise it, pin and return it.
    Page* new_page(int& out_page_id) {
        std::lock_guard<std::recursive_mutex> lk(mu_);
        int page_id = dm_->allocate_page();
        int fid = pick_frame();
        Frame& f = frames_[fid];
        f.page.init();
        f.page_id = page_id;
        // Durably persist the EMPTY page structure (header with next_page = -1)
        // right away. This makes page allocation a "forced" operation: even if a
        // crash later drops the tuple data we add, the on-disk page is still a
        // valid empty page rather than a block of zeros (whose next_page would
        // read as 0 and create a self-referential page chain).
        dm_->write_page(page_id, f.page.data);
        f.dirty = false;
        f.pin_count = 1;
        f.usage_count = 1;
        table_[page_id] = fid;
        out_page_id = page_id;
        return &f.page;
    }

    void unpin(int page_id, bool dirty) {
        std::lock_guard<std::recursive_mutex> lk(mu_);
        auto it = table_.find(page_id);
        if (it == table_.end()) return;
        Frame& f = frames_[it->second];
        if (dirty) f.dirty = true;
        if (f.pin_count > 0) f.pin_count--;
    }

    void flush_page(int page_id) {
        std::lock_guard<std::recursive_mutex> lk(mu_);
        auto it = table_.find(page_id);
        if (it == table_.end()) return;
        Frame& f = frames_[it->second];
        if (f.dirty) { dm_->write_page(f.page_id, f.page.data); f.dirty = false; }
    }

    void flush_all() {
        std::lock_guard<std::recursive_mutex> lk(mu_);
        for (auto& f : frames_)
            if (f.page_id != INVALID_PAGE_ID && f.dirty) {
                dm_->write_page(f.page_id, f.page.data);
                f.dirty = false;
            }
    }

    // Drop every cached page WITHOUT flushing - used to simulate a crash where
    // dirty pages never reached disk (recovery must rebuild them from the WAL).
    void evict_all_without_flush() {
        std::lock_guard<std::recursive_mutex> lk(mu_);
        for (auto& f : frames_) { f = Frame(); }
        table_.clear();
        hand_ = 0;
    }

    DiskManager* disk() { return dm_; }

private:
    // ClockSweep victim selection.
    int pick_frame() {
        // Prefer a never-used empty frame first.
        for (int i = 0; i < capacity_; ++i)
            if (frames_[i].page_id == INVALID_PAGE_ID) return i;

        for (int scanned = 0; scanned < 2 * capacity_ + 1; ++scanned) {
            Frame& f = frames_[hand_];
            if (f.pin_count == 0) {
                if (f.usage_count == 0) {
                    int victim = hand_;
                    if (f.dirty) dm_->write_page(f.page_id, f.page.data);
                    table_.erase(f.page_id);
                    hand_ = (hand_ + 1) % capacity_;
                    frames_[victim] = Frame();
                    return victim;
                }
                f.usage_count--;
            }
            hand_ = (hand_ + 1) % capacity_;
        }
        throw std::runtime_error("BufferPool: all frames pinned, cannot evict");
    }

    DiskManager*                    dm_;
    int                             capacity_;
    std::vector<Frame>              frames_;
    std::unordered_map<int,int>     table_; // page_id -> frame index
    int                             hand_ = 0;
    std::recursive_mutex            mu_;
};

} // namespace minidb
