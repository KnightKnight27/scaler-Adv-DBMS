// Lab 3: Clock Sweep (Clock) Buffer Pool Page Replacement Algorithm
// Compile: g++ -std=c++17 -o clocksweep clocksweep.cpp
// Run:     ./clocksweep
//
// Mirrors PostgreSQL's buffer manager algorithm:
//   - Circular hand sweeps frames
//   - usage_count (0..5): incremented on hit, decremented on sweep
//   - Pinned frames are never evicted

#include <iostream>
#include <vector>
#include <unordered_map>
#include <string>
#include <algorithm>
#include <iomanip>

// ── Frame state ──
struct Frame {
    int page_id = -1;          // -1 = empty
    int usage_count = 0;       // 0..5 (capped)
    bool pinned = false;
};

class BufferPool {
    std::vector<Frame>              frames;
    std::unordered_map<int, int>    page_to_frame;   // page_id → frame idx
    int                             hand = 0;         // clock hand position
    int                             capacity;

public:
    explicit BufferPool(int cap) : frames(cap), capacity(cap) {
        std::cout << "BufferPool created with " << cap << " frames\n\n";
    }

    // Fetch page: HIT if already in pool, MISS+EVICT otherwise.
    int fetch(int page_id) {
        // ── Already in pool: usage_count++, cap at 5 ──
        auto it = page_to_frame.find(page_id);
        if (it != page_to_frame.end()) {
            int idx = it->second;
            frames[idx].usage_count = std::min(frames[idx].usage_count + 1, 5);
            std::cout << "[HIT]   page " << std::setw(3) << page_id
                      << " in frame " << idx
                      << "  usage=" << frames[idx].usage_count << "\n";
            return idx;
        }

        // ── Need to evict via ClockSweep ──
        int victim = clocksweep();
        if (victim == -1) {
            std::cerr << "[ERR]   all frames pinned, cannot evict\n";
            return -1;
        }

        // Evict old occupant
        if (frames[victim].page_id != -1) {
            std::cout << "[EVICT] page " << std::setw(3) << frames[victim].page_id
                      << " from frame " << victim << "\n";
            page_to_frame.erase(frames[victim].page_id);
        }

        // Load new page with usage_count = 1
        frames[victim] = {page_id, 1, false};
        page_to_frame[page_id] = victim;
        std::cout << "[MISS]  page " << std::setw(3) << page_id
                  << " -> frame " << victim << "  usage=1\n";
        return victim;
    }

    // Pin a frame (prevent eviction)
    void pin(int page_id) {
        auto it = page_to_frame.find(page_id);
        if (it != page_to_frame.end())
            frames[it->second].pinned = true;
    }

    // Unpin a frame (allow eviction again)
    void unpin(int page_id) {
        auto it = page_to_frame.find(page_id);
        if (it != page_to_frame.end())
            frames[it->second].pinned = false;
    }

    void print_state() const {
        std::cout << "\n── Buffer Pool State (hand=" << hand << ") ──\n";
        for (int i = 0; i < capacity; i++) {
            const auto& f = frames[i];
            std::cout << "  Frame[" << std::setw(2) << i << "]  page="
                      << std::setw(3) << (f.page_id == -1 ? 0 : f.page_id)
                      << "  usage=" << f.usage_count
                      << (f.pinned ? " [PINNED]" : "")
                      << (i == hand  ? " <-- HAND" : "")
                      << "\n";
        }
        std::cout << "──────────────────────────────────────────\n\n";
    }

private:
    // ── ClockSweep algorithm ──
    // Walks the circular array looking for a frame with usage_count == 0
    // that is not pinned. Gives pages a "second chance" by decrementing
    // instead of evicting.
    int clocksweep() {
        int checked = 0;
        const int max_checks = 2 * capacity;   // two full rounds max

        while (checked < max_checks) {
            Frame& f = frames[hand];

            if (!f.pinned) {
                if (f.usage_count == 0) {
                    int victim = hand;
                    hand = (hand + 1) % capacity;
                    return victim;
                }
                // Second chance: decrement and keep going
                f.usage_count--;
            }

            hand = (hand + 1) % capacity;
            checked++;
        }
        return -1;  // all frames pinned
    }
};

// ── Demo ──
int main() {
    BufferPool pool(4);   // 4-frame buffer pool

    std::cout << "=== Access pattern: 1 2 3 4 1 2 5 1 2 3 4 5 ===\n";
    std::cout << "=== All initially MISS, then see hits and evictions ===\n\n";

    std::vector<int> accesses = {1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5};
    for (int page : accesses) {
        pool.fetch(page);
    }
    pool.print_state();

    std::cout << "=== Second round: same pattern, mostly HITs now ===\n\n";
    for (int page : accesses) {
        pool.fetch(page);
    }
    pool.print_state();

    std::cout << "=== PIN test: pin page 5, then access pages that would evict it ===\n\n";
    pool.pin(5);
    std::cout << "Pinned page 5. Now accessing pages 6 7 8 (must not evict 5):\n";
    pool.fetch(6);
    pool.fetch(7);
    pool.fetch(8);
    pool.fetch(9);       // may need to sweep past the pinned page
    pool.print_state();

    pool.unpin(5);
    std::cout << "Unpinned page 5.\n";
    pool.print_state();

    return 0;
}
