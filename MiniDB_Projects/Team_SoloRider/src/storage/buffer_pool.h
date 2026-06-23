#pragma once
// storage/buffer_pool.h — Page-level buffer pool with LRU-K eviction.
//
// The buffer pool is an array of `pool_size` page frames that live in
// memory.  It uses the LRU-K replacement policy:
//   • Among all unpinned frames, evict the one whose K-th most recent
//     access timestamp is the oldest (largest backward K-distance).
//   • If a frame has fewer than K recorded accesses its backward
//     K-distance is treated as +∞ and it is evicted first (FIFO among
//     such frames).
//
// Clients fetch pages, pin them while in use, unpin when done.
// Dirty pages are flushed to disk through the HeapFile.

#include <cstdint>
#include <list>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "common/types.h"
#include "storage/heap_file.h"
#include "storage/page.h"

namespace minidb {

class BufferPool {
public:
    BufferPool(HeapFile* heap_file, size_t pool_size, size_t k = 2);

    // Fetch a page into the pool.  If already present this is a *hit*;
    // otherwise a *miss* that may require eviction.
    // The returned pointer is valid until the page is evicted.
    // The page is automatically pinned (pin_count incremented).
    Page* fetch_page(page_id_t pid);

    // Unpin a page.  `is_dirty` should be true if the caller modified it.
    void unpin_page(page_id_t pid, bool is_dirty);

    // Flush a specific dirty page to disk.
    void flush_page(page_id_t pid);

    // Flush all dirty pages to disk.
    void flush_all_pages();

    // ── Statistics ──
    uint64_t get_hit_count()  const { return hits_;   }
    uint64_t get_miss_count() const { return misses_; }

private:
    size_t pool_size_;
    size_t k_;                         // the K in LRU-K

    std::vector<Page>       frames_;
    std::unordered_map<page_id_t, frame_id_t> page_table_;

    std::vector<int>        pin_counts_;
    std::vector<bool>       dirty_flags_;
    std::vector<page_id_t>  frame_to_page_;

    // Each frame keeps the timestamps of its last K accesses.
    std::vector<std::vector<uint64_t>> access_history_;
    uint64_t current_timestamp_ = 0;

    std::list<frame_id_t>   free_list_;
    HeapFile*               heap_file_;   // non-owning

    uint64_t hits_   = 0;
    uint64_t misses_ = 0;

    // ── Private helpers ──
    // LRU-K victim selection.  Throws if all frames are pinned.
    frame_id_t find_victim();

    // Record an access for a frame.
    void record_access(frame_id_t fid);
};

}  // namespace minidb
