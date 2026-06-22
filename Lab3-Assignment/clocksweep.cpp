// Lab 3 — ClockSweep (Clock) buffer-pool page-replacement algorithm.
// Mirrors the victim-selection logic in PostgreSQL's buffer manager
// (src/backend/storage/buffer/freelist.c): a circular "clock hand" plus a
// per-frame usage_count (0..5) that approximates LRU without an ordered list.
//
// Build: g++ -std=c++17 -O2 -o clocksweep clocksweep.cpp
#include <iostream>
#include <vector>
#include <unordered_map>
#include <string>
#include <algorithm>

struct Frame {
    int  page_id     = -1;   // -1 = empty
    int  usage_count = 0;    // PostgreSQL caps this at 5 (BM_MAX_USAGE_COUNT)
    bool pinned      = false;
};

class BufferPool {
    std::vector<Frame>          frames;
    std::unordered_map<int,int> page_to_frame;  // page_id -> frame index
    int                         hand = 0;        // clock hand
    int                         capacity;

    // running counters so we can report the hit ratio at the end
    long hits = 0, misses = 0, evictions = 0;

public:
    explicit BufferPool(int cap) : frames(cap), capacity(cap) {}

    // Fetch (pin) a page into the pool, loading it on a miss.
    // Returns the frame index, or -1 if every frame is pinned.
    int fetch(int page_id) {
        auto it = page_to_frame.find(page_id);
        if (it != page_to_frame.end()) {                 // ---- HIT ----
            int idx = it->second;
            frames[idx].usage_count = std::min(frames[idx].usage_count + 1, 5);
            ++hits;
            std::cout << "[HIT]   page " << page_id << " in frame " << idx
                      << " usage=" << frames[idx].usage_count << "\n";
            return idx;
        }

        int victim = clocksweep();                       // ---- MISS ----
        if (victim == -1) {
            std::cerr << "[ERR]   all frames pinned, cannot evict\n";
            return -1;
        }
        if (frames[victim].page_id != -1) {
            ++evictions;
            std::cout << "[EVICT] page " << frames[victim].page_id
                      << " from frame " << victim
                      << " (usage hit 0)\n";
            page_to_frame.erase(frames[victim].page_id);
        }
        frames[victim] = {page_id, 1, false};            // fresh page enters with usage=1
        page_to_frame[page_id] = victim;
        ++misses;
        std::cout << "[MISS]  page " << page_id << " loaded into frame " << victim << "\n";
        return victim;
    }

    void pin(int page_id)   { if (auto it = page_to_frame.find(page_id); it != page_to_frame.end()) frames[it->second].pinned = true;  }
    void unpin(int page_id) { if (auto it = page_to_frame.find(page_id); it != page_to_frame.end()) frames[it->second].pinned = false; }

    void print_state() const {
        std::cout << "\n--- Buffer Pool State (hand=" << hand << ") ---\n";
        for (int i = 0; i < capacity; i++) {
            const auto& f = frames[i];
            std::cout << "Frame[" << i << "] page="
                      << (f.page_id == -1 ? std::string("--") : std::to_string(f.page_id))
                      << " usage=" << f.usage_count
                      << (f.pinned ? " [PINNED]" : "")
                      << (i == hand ? "   <-- hand" : "")
                      << "\n";
        }
        std::cout << "-------------------------------\n";
    }

    void print_stats() const {
        long total = hits + misses;
        std::cout << "Accesses=" << total << "  hits=" << hits
                  << "  misses=" << misses << "  evictions=" << evictions
                  << "  hit_ratio="
                  << (total ? (100.0 * hits / total) : 0.0) << "%\n";
    }

private:
    // Sweep the circular array, giving each non-pinned frame a "second chance"
    // by decrementing usage_count; evict the first frame whose count is 0.
    int clocksweep() {
        int checked = 0;
        while (checked < 2 * capacity) {        // at most two full sweeps
            Frame& f = frames[hand];
            if (!f.pinned) {
                if (f.usage_count == 0) {
                    int victim = hand;
                    hand = (hand + 1) % capacity;
                    return victim;
                }
                f.usage_count--;                // second chance
            }
            hand = (hand + 1) % capacity;
            checked++;
        }
        return -1;                              // every frame pinned
    }
};

int main() {
    BufferPool pool(4);   // 4-frame buffer pool

    // Access pattern: pages 1,2 are "hot" (touched repeatedly early), so they
    // accumulate usage_count and should survive the eviction of colder pages.
    std::vector<int> accesses = {1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5};

    std::cout << "Access sequence:";
    for (int p : accesses) std::cout << " " << p;
    std::cout << "\n\n";

    for (int page : accesses) pool.fetch(page);

    pool.print_state();
    pool.print_stats();
    return 0;
}
