// Name: Lavya Tanotra
// Roll No: 24BCS10124
// Lab 3: Clock Sweep Page Replacement Algorithm
//
// PostgreSQL's buffer manager uses ClockSweep (not LRU) to evict pages.
// Each frame has a usage_count (0-5). The clock hand sweeps circularly:
//   - usage_count > 0  → decrement and skip (second chance)
//   - usage_count == 0 → evict (unless pinned)
//
// Benefits over plain LRU:
//   - No sorted list to maintain; O(1) per access with minimal contention
//   - usage_count cap (5) limits damage from sequential scan flooding
//   - Hot pages (count=5) survive a full-table scan that only increments by 1

#include <iostream>
#include <vector>
#include <unordered_map>
#include <string>

struct Frame {
    int  page_id     = -1;
    int  usage_count = 0;
    bool pinned      = false;
};

class BufferPool {
    std::vector<Frame>          frames;
    std::unordered_map<int,int> page_to_frame;
    int                         hand = 0;
    int                         capacity;

public:
    explicit BufferPool(int cap) : frames(cap), capacity(cap) {}

    int fetch(int page_id) {
        auto it = page_to_frame.find(page_id);
        if (it != page_to_frame.end()) {
            int idx = it->second;
            frames[idx].usage_count = std::min(frames[idx].usage_count + 1, 5);
            std::cout << "[HIT]   page " << page_id
                      << " frame=" << idx
                      << " usage=" << frames[idx].usage_count << "\n";
            return idx;
        }

        int victim = clocksweep();
        if (victim == -1) {
            std::cerr << "[ERR]   all frames pinned\n";
            return -1;
        }

        if (frames[victim].page_id != -1) {
            std::cout << "[EVICT] page " << frames[victim].page_id
                      << " from frame=" << victim << "\n";
            page_to_frame.erase(frames[victim].page_id);
        }

        frames[victim] = {page_id, 1, false};
        page_to_frame[page_id] = victim;
        std::cout << "[MISS]  page " << page_id
                  << " → frame=" << victim << " usage=1\n";
        return victim;
    }

    void pin(int page_id) {
        auto it = page_to_frame.find(page_id);
        if (it != page_to_frame.end()) frames[it->second].pinned = true;
    }

    void unpin(int page_id) {
        auto it = page_to_frame.find(page_id);
        if (it != page_to_frame.end()) frames[it->second].pinned = false;
    }

    void print_state() const {
        std::cout << "\n--- Buffer Pool (hand=" << hand << ") ---\n";
        for (int i = 0; i < capacity; ++i) {
            const auto& f = frames[i];
            std::cout << "  Frame[" << i << "] page="
                      << (f.page_id == -1 ? "--" : std::to_string(f.page_id))
                      << " usage=" << f.usage_count
                      << (f.pinned    ? " [PINNED]" : "")
                      << (i == hand   ? " <-- hand" : "") << "\n";
        }
        std::cout << "----------------------------------\n";
    }

private:
    int clocksweep() {
        for (int checked = 0; checked < 2 * capacity; ++checked) {
            Frame& f = frames[hand];
            int cur  = hand;
            hand = (hand + 1) % capacity;
            if (!f.pinned) {
                if (f.usage_count == 0) return cur;
                f.usage_count--;
            }
        }
        return -1;
    }
};

int main() {
    BufferPool pool(4);

    std::cout << "=== Access sequence: 1 2 3 4 1 2 5 1 2 3 4 5 ===\n\n";
    for (int p : {1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5})
        pool.fetch(p);

    pool.print_state();

    std::cout << "\n=== Pin frame holding page 1, then evict ===\n";
    pool.pin(1);
    pool.fetch(99);  // triggers clocksweep; page 1 survives (pinned)
    pool.print_state();

    return 0;
}

// Compile: g++ -std=c++17 -o clocksweep clocksweep.cpp
// Run:     ./clocksweep
