// storage/buffer_pool.cpp — Buffer pool with LRU-K replacement.
//
// See buffer_pool.h for the full design rationale.

#include "storage/buffer_pool.h"

#include <algorithm>
#include <limits>

namespace minidb {

// ─── Constructor ────────────────────────────────────────────
BufferPool::BufferPool(HeapFile* heap_file, size_t pool_size, size_t k)
    : pool_size_(pool_size),
      k_(k),
      frames_(pool_size),
      pin_counts_(pool_size, 0),
      dirty_flags_(pool_size, false),
      frame_to_page_(pool_size, INVALID_PAGE_ID),
      access_history_(pool_size),
      heap_file_(heap_file)
{
    for (size_t i = 0; i < pool_size; ++i) {
        free_list_.push_back(static_cast<frame_id_t>(i));
    }
}

// ─── fetch_page ─────────────────────────────────────────────
Page* BufferPool::fetch_page(page_id_t pid) {
    // 1) Check if the page is already in the pool (HIT).
    auto it = page_table_.find(pid);
    if (it != page_table_.end()) {
        frame_id_t fid = it->second;
        pin_counts_[fid]++;
        record_access(fid);
        hits_++;
        return &frames_[fid];
    }

    // 2) MISS — need a free frame.
    frame_id_t fid;
    if (!free_list_.empty()) {
        fid = free_list_.front();
        free_list_.pop_front();
    } else {
        fid = find_victim();

        // Flush the victim if dirty.
        if (dirty_flags_[fid]) {
            heap_file_->write_page(frame_to_page_[fid], frames_[fid]);
            dirty_flags_[fid] = false;
        }

        // Remove old mapping.
        page_table_.erase(frame_to_page_[fid]);
    }

    // 3) Read the new page from disk into the frame.
    heap_file_->read_page(pid, frames_[fid]);

    // 4) Set up bookkeeping.
    page_table_[pid]    = fid;
    frame_to_page_[fid] = pid;
    pin_counts_[fid]    = 1;
    dirty_flags_[fid]   = false;
    access_history_[fid].clear();
    record_access(fid);

    misses_++;
    return &frames_[fid];
}

// ─── unpin_page ─────────────────────────────────────────────
void BufferPool::unpin_page(page_id_t pid, bool is_dirty) {
    auto it = page_table_.find(pid);
    if (it == page_table_.end()) return;  // not in pool

    frame_id_t fid = it->second;
    if (pin_counts_[fid] > 0) {
        pin_counts_[fid]--;
    }
    if (is_dirty) {
        dirty_flags_[fid] = true;
    }
}

// ─── flush_page ─────────────────────────────────────────────
void BufferPool::flush_page(page_id_t pid) {
    auto it = page_table_.find(pid);
    if (it == page_table_.end()) return;

    frame_id_t fid = it->second;
    if (dirty_flags_[fid]) {
        heap_file_->write_page(pid, frames_[fid]);
        dirty_flags_[fid] = false;
    }
}

// ─── flush_all_pages ────────────────────────────────────────
void BufferPool::flush_all_pages() {
    for (size_t fid = 0; fid < pool_size_; ++fid) {
        if (frame_to_page_[fid] != INVALID_PAGE_ID && dirty_flags_[fid]) {
            heap_file_->write_page(frame_to_page_[fid], frames_[fid]);
            dirty_flags_[fid] = false;
        }
    }
}

// ─── find_victim (LRU-K) ───────────────────────────────────
// Among all unpinned frames, the victim is the one with the largest
// backward K-distance.  If a frame has fewer than K accesses, its
// distance is +∞ (evict these first, FIFO = smallest earliest timestamp
// among them).
frame_id_t BufferPool::find_victim() {
    frame_id_t victim     = static_cast<frame_id_t>(pool_size_);  // sentinel
    bool       victim_inf = false;
    uint64_t   victim_ts  = 0;  // the K-th-from-back timestamp of victim

    for (size_t fid = 0; fid < pool_size_; ++fid) {
        if (pin_counts_[fid] != 0) continue;                      // skip pinned
        if (frame_to_page_[fid] == INVALID_PAGE_ID) continue;     // empty frame

        const auto& hist = access_history_[fid];
        bool has_k = hist.size() >= k_;

        if (!has_k) {
            // Backward K-distance is +∞.  Among these we use FIFO:
            // evict the one with the smallest *earliest* access timestamp.
            uint64_t earliest = hist.empty() ? 0 : hist.front();
            if (!victim_inf || earliest < victim_ts ||
                victim == static_cast<frame_id_t>(pool_size_)) {
                victim     = static_cast<frame_id_t>(fid);
                victim_inf = true;
                victim_ts  = earliest;
            }
        } else if (!victim_inf) {
            // Finite backward K-distance: current_timestamp_ - hist[hist.size()-k].
            // We want the *largest* distance ⇔ the *smallest* K-th timestamp.
            uint64_t kth_ts = hist[hist.size() - k_];
            if (victim == static_cast<frame_id_t>(pool_size_) || kth_ts < victim_ts) {
                victim    = static_cast<frame_id_t>(fid);
                victim_ts = kth_ts;
            }
        }
        // If victim already has +∞ distance, finite candidates cannot beat it.
    }

    if (victim == static_cast<frame_id_t>(pool_size_)) {
        throw std::runtime_error("BufferPool: all frames are pinned, cannot evict");
    }
    return victim;
}

// ─── record_access ──────────────────────────────────────────
void BufferPool::record_access(frame_id_t fid) {
    access_history_[fid].push_back(current_timestamp_);
    // Keep only the last K entries.
    if (access_history_[fid].size() > k_) {
        access_history_[fid].erase(access_history_[fid].begin());
    }
    current_timestamp_++;
}

}  // namespace minidb
