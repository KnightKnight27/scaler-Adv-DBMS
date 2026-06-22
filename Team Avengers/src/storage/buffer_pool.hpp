// ============================================================================
//  buffer_pool.hpp — Caches a bounded set of pages in memory.
//
//  The buffer pool is the single source of truth for "the current contents of
//  page X". Higher layers NEVER read the disk directly; they fetch a page,
//  read/write its bytes, then unpin it. Three jobs:
//
//    1. Page table   : page_id -> frame  (is this page already resident?)
//    2. Pinning      : a pinned page is in use and must not be evicted
//    3. Replacement  : when all frames are full, the CLOCK-SWEEP algorithm
//                      (second-chance) picks an unpinned victim to evict.
//
//  The clock replacer is the same second-chance scheme from Lab 3: each frame
//  carries a reference bit; the hand sweeps, clearing set bits (a "second
//  chance") and evicting the first frame whose bit is already clear. It
//  approximates LRU at O(1) amortised cost with no per-access list surgery.
// ============================================================================
#pragma once

#include "../common/types.hpp"
#include "disk_manager.hpp"

#include <array>
#include <cstring>
#include <list>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace minidb {

// A frame's worth of bytes plus the bookkeeping the pool needs about it.
struct Page {
    char        data[PAGE_SIZE] = {};
    page_id_t   page_id  = INVALID_PAGE_ID;
    int         pin_count = 0;       // # of callers currently using this page
    bool        is_dirty = false;    // modified since it was read from disk?

    void reset() {
        std::memset(data, 0, PAGE_SIZE);
        page_id = INVALID_PAGE_ID;
        pin_count = 0;
        is_dirty = false;
    }
};

// ---------------------------------------------------------------------------
//  CLOCK-SWEEP replacer. Tracks only the frames currently eligible for
//  eviction (i.e. unpinned). Pinned frames are removed from its view.
// ---------------------------------------------------------------------------
class ClockReplacer {
public:
    explicit ClockReplacer(size_t num_frames)
        : ref_bit_(num_frames, false), in_clock_(num_frames, false) {}

    // Choose a victim frame to evict. Returns false if nothing is evictable
    // (every resident page is pinned). On success *victim holds the frame id.
    bool victim(frame_id_t* victim) {
        if (size_ == 0) return false;
        // Sweep the hand around the circular buffer. A frame with its ref bit
        // SET gets a second chance (bit cleared, hand advances). The first
        // frame found with ref bit CLEAR is evicted.
        while (true) {
            if (in_clock_[hand_]) {
                if (ref_bit_[hand_]) {
                    ref_bit_[hand_] = false;          // consume the second chance
                } else {
                    *victim = hand_;                   // cold frame: evict it
                    in_clock_[hand_] = false;
                    --size_;
                    advance();
                    return true;
                }
            }
            advance();
        }
    }

    // Called on pin: the page is in use, so it must not be a victim candidate.
    void pin(frame_id_t f) {
        if (in_clock_[f]) { in_clock_[f] = false; --size_; }
    }

    // Called when pin_count hits 0: page becomes evictable again, ref bit set
    // (it was just used) so it survives at least one hand pass.
    void unpin(frame_id_t f) {
        if (!in_clock_[f]) { in_clock_[f] = true; ref_bit_[f] = true; ++size_; }
    }

    size_t size() const { return size_; }

private:
    void advance() { hand_ = (hand_ + 1) % ref_bit_.size(); }

    std::vector<bool> ref_bit_;   // second-chance bit per frame
    std::vector<bool> in_clock_;  // is this frame currently evictable?
    size_t            hand_ = 0;  // the sweeping clock hand
    size_t            size_ = 0;  // # of evictable frames
};

// ---------------------------------------------------------------------------
//  BufferPoolManager
// ---------------------------------------------------------------------------
class BufferPoolManager {
public:
    BufferPoolManager(size_t pool_size, DiskManager* dm)
        : disk_(dm), replacer_(pool_size) {
        frames_.resize(pool_size);
        // Initially every frame is free.
        for (frame_id_t i = 0; i < static_cast<frame_id_t>(pool_size); ++i)
            free_list_.push_back(i);
    }

    // Allocate a brand-new page (on disk + a resident frame), returned pinned.
    Page* new_page(page_id_t* page_id) {
        std::lock_guard<std::mutex> g(latch_);
        frame_id_t fid;
        if (!grab_frame(&fid)) return nullptr;  // pool full and all pinned

        page_id_t pid = disk_->allocate_page();
        Page& p = frames_[fid];
        p.reset();
        p.page_id = pid;
        p.pin_count = 1;
        p.is_dirty = true;        // a fresh page differs from disk (all zeros)
        page_table_[pid] = fid;
        replacer_.pin(fid);
        *page_id = pid;
        return &p;
    }

    // Fetch an existing page, reading it from disk if not already resident.
    // Returned pinned: caller MUST unpin_page when done.
    Page* fetch_page(page_id_t pid) {
        std::lock_guard<std::mutex> g(latch_);
        auto it = page_table_.find(pid);
        if (it != page_table_.end()) {        // cache hit
            frame_id_t fid = it->second;
            frames_[fid].pin_count++;
            replacer_.pin(fid);
            return &frames_[fid];
        }
        // cache miss: need a frame, then read the page in.
        frame_id_t fid;
        if (!grab_frame(&fid)) return nullptr;
        Page& p = frames_[fid];
        p.reset();
        p.page_id = pid;
        p.pin_count = 1;
        disk_->read_page(pid, p.data);
        page_table_[pid] = fid;
        replacer_.pin(fid);
        return &p;
    }

    // Release a page. is_dirty=true marks it modified (so eviction flushes it).
    // When pin_count reaches 0 the page becomes an eviction candidate.
    bool unpin_page(page_id_t pid, bool is_dirty) {
        std::lock_guard<std::mutex> g(latch_);
        auto it = page_table_.find(pid);
        if (it == page_table_.end()) return false;
        frame_id_t fid = it->second;
        Page& p = frames_[fid];
        if (p.pin_count <= 0) return false;
        if (is_dirty) p.is_dirty = true;
        if (--p.pin_count == 0) replacer_.unpin(fid);
        return true;
    }

    // Force one page to disk regardless of pin state (used by checkpoint/flush).
    bool flush_page(page_id_t pid) {
        std::lock_guard<std::mutex> g(latch_);
        auto it = page_table_.find(pid);
        if (it == page_table_.end()) return false;
        Page& p = frames_[it->second];
        disk_->write_page(pid, p.data);
        p.is_dirty = false;
        return true;
    }

    void flush_all() {
        std::lock_guard<std::mutex> g(latch_);
        for (auto& [pid, fid] : page_table_)
            if (frames_[fid].is_dirty) {
                disk_->write_page(pid, frames_[fid].data);
                frames_[fid].is_dirty = false;
            }
    }

private:
    // Obtain a usable frame: prefer the free list, else evict a clock victim.
    // WAL rule: a dirty victim is written back before its frame is reused.
    bool grab_frame(frame_id_t* out) {
        if (!free_list_.empty()) {
            *out = free_list_.front();
            free_list_.pop_front();
            return true;
        }
        frame_id_t victim;
        if (!replacer_.victim(&victim)) return false;  // everything pinned
        Page& v = frames_[victim];
        if (v.is_dirty) disk_->write_page(v.page_id, v.data);
        page_table_.erase(v.page_id);
        *out = victim;
        return true;
    }

    DiskManager* disk_;
    std::vector<Page> frames_;                          // the actual cache
    std::unordered_map<page_id_t, frame_id_t> page_table_;  // pid -> frame
    std::list<frame_id_t> free_list_;                   // never-used frames
    ClockReplacer replacer_;
    std::mutex latch_;                                  // guards all of the above
};

}  // namespace minidb
