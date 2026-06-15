// Lab 3: ClockSweep buffer pool replacement
// Aditya Bhaskara (24BCS10058)
//
// This is the eviction strategy PostgreSQL uses for its shared buffer pool.
// Instead of keeping an ordered LRU list, every frame holds a small usage
// counter and a "clock hand" sweeps the frames in a circle. A frame only gets
// evicted once its usage counter has decayed to zero, which gives recently
// touched pages a second chance and approximates LRU at O(1) per access.
//
// Build: g++ -std=c++17 -o clock_sweep clock_sweep.cpp
// Run:   ./clock_sweep

#include <algorithm>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

// PostgreSQL caps the usage counter at 5 (BM_MAX_USAGE_COUNT). A sequential
// scan can only bump a page once, so genuinely hot pages stay above the flood.
constexpr int kMaxUsageCount = 5;

struct Frame {
    int  page_id     = -1;     // -1 marks an empty frame
    int  usage_count = 0;
    bool pinned      = false;
};

class BufferPool {
public:
    explicit BufferPool(int capacity) : frames_(capacity), capacity_(capacity) {}

    // Bring a page into the pool, loading it if it is not already resident.
    // Returns the frame index, or -1 if every frame is currently pinned.
    int fetch(int page_id) {
        if (auto it = page_to_frame_.find(page_id); it != page_to_frame_.end()) {
            Frame& f = frames_[it->second];
            f.usage_count = std::min(f.usage_count + 1, kMaxUsageCount);
            std::cout << "[HIT]   page " << page_id << " -> frame " << it->second
                      << " (usage=" << f.usage_count << ")\n";
            return it->second;
        }

        int victim = pick_victim();
        if (victim == -1) {
            std::cout << "[ERROR] every frame is pinned, cannot load page " << page_id << "\n";
            return -1;
        }

        Frame& f = frames_[victim];
        if (f.page_id != -1) {
            std::cout << "[EVICT] page " << f.page_id << " from frame " << victim << "\n";
            page_to_frame_.erase(f.page_id);
        }

        f = {page_id, 1, false};
        page_to_frame_[page_id] = victim;
        std::cout << "[MISS]  page " << page_id << " -> frame " << victim << " (usage=1)\n";
        return victim;
    }

    void pin(int page_id)   { if (Frame* f = find(page_id)) f->pinned = true; }
    void unpin(int page_id) { if (Frame* f = find(page_id)) f->pinned = false; }

    void print_state() const {
        std::cout << "\n--- buffer pool (hand at frame " << hand_ << ") ---\n";
        for (int i = 0; i < capacity_; ++i) {
            const Frame& f = frames_[i];
            std::cout << "  frame[" << i << "] page="
                      << (f.page_id == -1 ? "--" : std::to_string(f.page_id))
                      << " usage=" << f.usage_count
                      << (f.pinned ? " [pinned]" : "")
                      << (i == hand_ ? "  <- hand" : "")
                      << "\n";
        }
        std::cout << "----------------------------------------\n\n";
    }

private:
    Frame* find(int page_id) {
        auto it = page_to_frame_.find(page_id);
        return it == page_to_frame_.end() ? nullptr : &frames_[it->second];
    }

    // Sweep the clock hand until a frame with usage_count 0 is found, decaying
    // every frame it passes. Two full laps is enough: the first lap drains the
    // counters, the second is guaranteed to land on a zero (unless all pinned).
    int pick_victim() {
        for (int steps = 0; steps < 2 * capacity_; ++steps) {
            Frame& f = frames_[hand_];
            if (!f.pinned) {
                if (f.usage_count == 0) {
                    int victim = hand_;
                    advance_hand();
                    return victim;
                }
                --f.usage_count;
            }
            advance_hand();
        }
        return -1;
    }

    void advance_hand() { hand_ = (hand_ + 1) % capacity_; }

    std::vector<Frame>          frames_;
    std::unordered_map<int,int> page_to_frame_;
    int                         hand_     = 0;
    int                         capacity_;
};

int main() {
    BufferPool pool(4);

    // A page reference string. Pages 1 and 2 are touched often, so they should
    // build up usage and survive the eviction sweeps longer than the others.
    const std::vector<int> accesses = {1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5};

    std::cout << "access pattern:";
    for (int p : accesses) std::cout << " " << p;
    std::cout << "\n\n";

    for (int page : accesses) {
        pool.fetch(page);
    }

    pool.print_state();
    return 0;
}
