#include <iostream>
#include <vector>
#include <unordered_map>
#include <cassert>
#include <iomanip>

/**
 * ClockSweep Buffer Pool Implementation
 * 
 * Implements PostgreSQL's Clock Sweep algorithm for buffer eviction.
 * Each frame carries a usage_count (0-5). The clock hand sweeps:
 * - If usage_count > 0: decrement and move on (second chance)
 * - If usage_count == 0: evict this frame and insert new page
 */

struct Frame {
    int     page_id    = -1;      // -1 = empty
    int     usage_count = 0;      // 0 to 5
    bool    pinned     = false;   // cannot evict if pinned
};

class BufferPool {
private:
    std::vector<Frame>              frames;
    std::unordered_map<int,int>     page_to_frame;  // page_id -> frame index
    int                             hand = 0;        // clock hand position
    int                             capacity;

public:
    explicit BufferPool(int cap) : frames(cap), capacity(cap) {}

    int fetch(int page_id) {
        // Case 1: Page already in pool
        auto it = page_to_frame.find(page_id);
        if (it != page_to_frame.end()) {
            int idx = it->second;
            frames[idx].usage_count = std::min(frames[idx].usage_count + 1, 5);
            std::cout << "[HIT]  page " << page_id
                      << " in frame " << idx
                      << " usage=" << frames[idx].usage_count << "\n";
            return idx;
        }

        // Case 2: Empty frame available
        for (int i = 0; i < capacity; ++i) {
            if (frames[i].page_id == -1) {
                frames[i].page_id = page_id;
                frames[i].usage_count = 1;
                page_to_frame[page_id] = i;
                std::cout << "[LOAD] page " << page_id
                          << " into empty frame " << i << "\n";
                return i;
            }
        }

        // Case 3: All frames occupied - use clock sweep
        int victim = clocksweep();
        if (victim == -1) {
            std::cerr << "[ERROR] All frames pinned!\n";
            return -1;
        }

        int old_page = frames[victim].page_id;
        page_to_frame.erase(old_page);
        
        frames[victim].page_id = page_id;
        frames[victim].usage_count = 1;
        page_to_frame[page_id] = victim;
        
        std::cout << "[EVICT] page " << old_page << " from frame " << victim
                  << ", loaded page " << page_id << "\n";
        return victim;
    }

    int clocksweep() {
        int start = hand;
        
        while (true) {
            // Skip pinned frames
            if (frames[hand].pinned) {
                hand = (hand + 1) % capacity;
                if (hand == start) {
                    std::cout << "[SWEEP] No unpinned frame found!\n";
                    return -1;  // All pinned
                }
                continue;
            }

            // Second chance: if usage_count > 0, decrement and move
            if (frames[hand].usage_count > 0) {
                frames[hand].usage_count--;
                std::cout << "[SWEEP] Frame " << hand
                          << " (page " << frames[hand].page_id
                          << ") gets second chance, usage=" << frames[hand].usage_count << "\n";
                hand = (hand + 1) % capacity;
            } else {
                // Found victim
                int victim = hand;
                hand = (hand + 1) % capacity;
                std::cout << "[SWEEP] Selected frame " << victim
                          << " (page " << frames[victim].page_id << ") as victim\n";
                return victim;
            }

            // Prevent infinite loop
            if (hand == start && frames[hand].usage_count == 0) {
                return hand;
            }
        }
    }

    void pin(int page_id) {
        auto it = page_to_frame.find(page_id);
        if (it != page_to_frame.end()) {
            frames[it->second].pinned = true;
            std::cout << "[PIN]  page " << page_id << "\n";
        }
    }

    void unpin(int page_id) {
        auto it = page_to_frame.find(page_id);
        if (it != page_to_frame.end()) {
            frames[it->second].pinned = false;
            std::cout << "[UNPIN] page " << page_id << "\n";
        }
    }

    void printState() {
        std::cout << "\n--- Buffer Pool State (hand=" << hand << ") ---\n";
        std::cout << std::left << std::setw(8) << "Frame"
                  << std::setw(10) << "Page ID"
                  << std::setw(8) << "Usage"
                  << std::setw(8) << "Pinned" << "\n";
        std::cout << std::string(34, '-') << "\n";
        
        for (int i = 0; i < capacity; ++i) {
            std::cout << std::left << std::setw(8) << i
                      << std::setw(10) << (frames[i].page_id == -1 ? "EMPTY" : std::to_string(frames[i].page_id))
                      << std::setw(8) << frames[i].usage_count
                      << std::setw(8) << (frames[i].pinned ? "YES" : "NO") << "\n";
        }
        std::cout << "\n";
    }
};

int main() {
    std::cout << "=== ClockSweep Buffer Pool Demonstration ===\n\n";
    
    BufferPool pool(4);  // 4 frames
    
    std::cout << "--- Scenario 1: Fill buffer pool ---\n";
    pool.fetch(1);
    pool.fetch(2);
    pool.fetch(3);
    pool.fetch(4);
    pool.printState();

    std::cout << "--- Scenario 2: Re-access page 1 (increases usage) ---\n";
    pool.fetch(1);
    pool.fetch(1);
    pool.printState();

    std::cout << "--- Scenario 3: Load new page 5 (triggers clock sweep) ---\n";
    pool.fetch(5);
    pool.printState();

    std::cout << "--- Scenario 4: Heavy workload (mix of access patterns) ---\n";
    pool.fetch(3);
    pool.fetch(3);
    pool.fetch(2);
    pool.fetch(6);  // Eviction
    pool.fetch(7);  // Eviction
    pool.printState();

    std::cout << "--- Scenario 5: Pin/Unpin ---\n";
    pool.pin(5);
    pool.fetch(8);  // Page 5 is pinned, cannot evict it
    pool.printState();
    
    pool.unpin(5);
    pool.fetch(9);  // Now page 5 can be evicted
    pool.printState();

    std::cout << "=== Clock Sweep Algorithm Complete ===\n";
    return 0;
}
