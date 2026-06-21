#include <iostream>
#include <vector>
#include <unordered_map>
#include <optional>
#include <string>
#include <algorithm>
#include <cassert>

/*
 * ClockSweep Buffer Pool — PostgreSQL's page-replacement algorithm.
 *
 * Each frame tracks:
 *   page_id     : which page occupies the slot (-1 = empty)
 *   usage_count : "hotness" counter, capped at MAX_USAGE (5 in PostgreSQL)
 *   pinned      : true while an upper layer holds a reference (page cannot be evicted)
 *
 * On a MISS the clock hand advances until it finds a candidate:
 *   - skip pinned frames entirely
 *   - skip frames with usage_count > 0, but decrement by 1 each time
 *   - evict the first frame with usage_count == 0 that is not pinned
 *
 * This approximates LRU without maintaining a sorted structure.
 */

static constexpr int MAX_USAGE = 5;

struct Frame {
    int  page_id    = -1;
    int  usage_count = 0;
    bool pinned     = false;

    bool empty()    const { return page_id == -1; }
    bool evictable() const { return !pinned && usage_count == 0; }
};

class BufferPool {
public:
    explicit BufferPool(int capacity)
        : capacity_(capacity), frames_(capacity), hand_(0) {
        assert(capacity > 0);
    }

    // Load a page into the pool.  Returns the frame index, -1 if all frames pinned.
    int fetch(int page_id) {
        auto it = page_map_.find(page_id);
        if (it != page_map_.end()) {
            int idx = it->second;
            frames_[idx].usage_count =
                std::min(frames_[idx].usage_count + 1, MAX_USAGE);
            log("[HIT ] page " + std::to_string(page_id) +
                " → frame " + std::to_string(idx) +
                "  usage=" + std::to_string(frames_[idx].usage_count));
            return idx;
        }

        int victim = sweep();
        if (victim == -1) {
            log("[FAIL] all frames pinned — cannot evict");
            return -1;
        }

        if (!frames_[victim].empty()) {
            log("[EVCT] page " + std::to_string(frames_[victim].page_id) +
                " evicted from frame " + std::to_string(victim));
            page_map_.erase(frames_[victim].page_id);
        }

        frames_[victim] = {page_id, 1, false};
        page_map_[page_id] = victim;
        log("[MISS] page " + std::to_string(page_id) +
            " loaded  → frame " + std::to_string(victim) +
            "  usage=1");
        return victim;
    }

    void pin(int page_id) {
        auto it = page_map_.find(page_id);
        if (it != page_map_.end()) frames_[it->second].pinned = true;
    }

    void unpin(int page_id) {
        auto it = page_map_.find(page_id);
        if (it != page_map_.end()) frames_[it->second].pinned = false;
    }

    void print_state(const std::string& label = "") const {
        if (!label.empty()) std::cout << "\n=== " << label << " ===\n";
        std::cout << "Hand → frame[" << hand_ << "]\n";
        std::cout << std::string(52, '-') << "\n";
        for (int i = 0; i < capacity_; ++i) {
            const Frame& f = frames_[i];
            std::cout << (i == hand_ ? ">" : " ")
                      << " frame[" << i << "]  ";
            if (f.empty()) {
                std::cout << "(empty)\n";
            } else {
                std::cout << "page=" << f.page_id
                          << "  usage=" << f.usage_count
                          << (f.pinned ? "  [PINNED]" : "")
                          << "\n";
            }
        }
        std::cout << std::string(52, '-') << "\n";
    }

private:
    // Advance the clock hand until an evictable frame is found.
    // On each pass, frames with usage_count > 0 get a decrement ("second chance").
    int sweep() {
        int passes = 0;
        int limit  = 2 * capacity_;   // at most two full sweeps

        while (passes < limit) {
            Frame& f = frames_[hand_];

            if (!f.pinned) {
                if (f.empty() || f.usage_count == 0) {
                    int victim = hand_;
                    hand_ = (hand_ + 1) % capacity_;
                    return victim;
                }
                f.usage_count--;   // second-chance decrement
            }

            hand_ = (hand_ + 1) % capacity_;
            ++passes;
        }
        return -1;   // all frames are pinned
    }

    static void log(const std::string& msg) { std::cout << msg << "\n"; }

    int               capacity_;
    std::vector<Frame> frames_;
    std::unordered_map<int, int> page_map_;   // page_id → frame index
    int               hand_;
};

// ── Demo ──────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "======================================================\n";
    std::cout << "  ClockSweep Buffer Pool (capacity = 4 frames)\n";
    std::cout << "  Demonstrates PostgreSQL-style page replacement\n";
    std::cout << "======================================================\n\n";

    BufferPool pool(4);

    // Access sequence that shows HIT promotion, eviction order, and pinning
    std::cout << "--- Phase 1: fill the pool (pages 1-4) ---\n";
    for (int pg : {1, 2, 3, 4}) pool.fetch(pg);
    pool.print_state("After initial fill");

    std::cout << "\n--- Phase 2: re-access pages 1 and 2 (boost usage) ---\n";
    pool.fetch(1);
    pool.fetch(1);
    pool.fetch(2);
    pool.print_state("After re-access of pages 1 & 2");

    std::cout << "\n--- Phase 3: pin page 3, then request page 5 (miss) ---\n";
    pool.pin(3);
    pool.fetch(5);
    pool.print_state("After pin(3) + fetch(5)");

    std::cout << "\n--- Phase 4: unpin page 3, request page 6 ---\n";
    pool.unpin(3);
    pool.fetch(6);
    pool.print_state("After unpin(3) + fetch(6)");

    std::cout << "\n--- Phase 5: sequential scan simulation (pages 7-10) ---\n";
    std::cout << "  (Sequential pages each get usage=1; hot pages 1 & 2\n";
    std::cout << "   survive longer because of higher usage count)\n";
    for (int pg : {7, 8, 9, 10}) pool.fetch(pg);
    pool.print_state("After sequential scan");

    return 0;
}
