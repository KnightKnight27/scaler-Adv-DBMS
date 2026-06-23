#include <iostream>
#include <vector>
#include <unordered_map>
#include <string>
#include <algorithm>

struct Frame {
    int page_id = -1;       // -1 means the frame is empty
    int usage_count = 0;    // Usage count for the clock algorithm
    bool pinned = false;    // Whether the page is pinned in memory
};

class BufferPool {
    std::vector<Frame> frames;
    std::unordered_map<int, int> page_to_frame;  // Maps page_id to frame index
    int hand = 0;           // The clock hand
    int capacity;           // Total number of frames in the pool

public:
    explicit BufferPool(int cap) : frames(cap), capacity(cap) {}

    // Pin a page into the buffer pool. It loads it if it isn't already present.
    // It returns the frame index, or -1 if all frames are currently pinned.
    int fetch(int page_id) {
        // First check if the page is already in the pool
        auto it = page_to_frame.find(page_id);
        if (it != page_to_frame.end()) {
            int idx = it->second;
            frames[idx].usage_count = std::min(frames[idx].usage_count + 1, 5);
            std::cout << "[HIT]  page " << page_id
                      << " in frame " << idx
                      << " usage=" << frames[idx].usage_count << "\n";
            return idx;
        }

        // If it's not in the pool, we need to find a victim frame
        int victim = clocksweep();
        if (victim == -1) {
            std::cerr << "[ERR]  all frames are pinned, cannot evict any page\n";
            return -1;
        }

        // Evict the current occupant if there is one
        if (frames[victim].page_id != -1) {
            std::cout << "[EVICT] page " << frames[victim].page_id
                      << " from frame " << victim << "\n";
            page_to_frame.erase(frames[victim].page_id);
        }

        // Load the new page into the victim frame
        frames[victim] = {page_id, 1, false};
        page_to_frame[page_id] = victim;
        std::cout << "[MISS] page " << page_id
                  << " loaded into frame " << victim << "\n";
        return victim;
    }

    void pin(int page_id) {
        auto it = page_to_frame.find(page_id);
        if (it != page_to_frame.end()) {
            frames[it->second].pinned = true;
        }
    }

    void unpin(int page_id) {
        auto it = page_to_frame.find(page_id);
        if (it != page_to_frame.end()) {
            frames[it->second].pinned = false;
        }
    }

    void print_state() const {
        std::cout << "\n--- Buffer Pool State (hand=" << hand << ") ---\n";
        for (int i = 0; i < capacity; i++) {
            const auto& f = frames[i];
            std::cout << "Frame[" << i << "] page="
                      << (f.page_id == -1 ? "--" : std::to_string(f.page_id))
                      << " usage=" << f.usage_count
                      << (f.pinned ? " [PINNED]" : "")
                      << (i == hand ? " <-- hand" : "")
                      << "\n";
        }
        std::cout << "-------------------------------\n\n";
    }

private:
    // Finds and returns the index of the frame to evict.
    int clocksweep() {
        int checked = 0;
        // We do at most two full sweeps
        while (checked < 2 * capacity) {
            Frame& f = frames[hand];
            if (!f.pinned) {
                if (f.usage_count == 0) {
                    int victim = hand;
                    hand = (hand + 1) % capacity;
                    return victim;
                }
                f.usage_count--;
            }
            hand = (hand + 1) % capacity;
            checked++;
        }
        return -1; // Everything is pinned
    }
};

int main() {
    // Let's create a small 4-frame buffer pool
    BufferPool pool(4);

    // This is the access pattern we'll simulate
    std::vector<int> accesses = {1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5};

    for (int page : accesses) {
        pool.fetch(page);
    }
    pool.print_state();

    return 0;
}
