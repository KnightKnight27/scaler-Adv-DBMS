#pragma once
#include <list>
#include <unordered_map>
#include <mutex>
#include "common/types.h"

namespace minidb {

// The replacer decides which *unpinned* frame to evict when the buffer pool is
// full. We use classic LRU: the least-recently-unpinned frame is victimised
// first. A frame becomes a candidate only after the buffer pool unpins it.
//
// Implementation: a doubly linked list in LRU order (front = least recent) plus
// a hash map from frame id to its list position for O(1) removal.
class LRUReplacer {
public:
    // Pick and remove the least-recently-used frame. Returns false if there
    // are no evictable frames. On success *frame_id holds the victim.
    bool victim(frame_id_t *frame_id) {
        std::lock_guard<std::mutex> guard(latch_);
        if (lru_list_.empty()) return false;
        *frame_id = lru_list_.front();
        lru_list_.pop_front();
        map_.erase(*frame_id);
        return true;
    }

    // Called when a frame is pinned again: it must not be evicted, so remove it
    // from the candidate set.
    void pin(frame_id_t frame_id) {
        std::lock_guard<std::mutex> guard(latch_);
        auto it = map_.find(frame_id);
        if (it != map_.end()) {
            lru_list_.erase(it->second);
            map_.erase(it);
        }
    }

    // Called when a frame's pin count hits zero: it becomes evictable and is
    // marked most-recently-used (pushed to the back).
    void unpin(frame_id_t frame_id) {
        std::lock_guard<std::mutex> guard(latch_);
        if (map_.find(frame_id) != map_.end()) return; // already a candidate
        lru_list_.push_back(frame_id);
        map_[frame_id] = std::prev(lru_list_.end());
    }

    size_t size() {
        std::lock_guard<std::mutex> guard(latch_);
        return lru_list_.size();
    }

private:
    std::list<frame_id_t>                                          lru_list_;
    std::unordered_map<frame_id_t, std::list<frame_id_t>::iterator> map_;
    std::mutex                                                     latch_;
};

} // namespace minidb
