#include <iostream>
#include <vector>
#include <unordered_map>
#include <string>
#include <iomanip>

/**
 * Frame: Represents a single buffer pool frame
 * - page_id: ID of the page stored (-1 = empty)
 * - usage_count: Reference count (0-5, PostgreSQL's approach)
 * - pinned: If true, frame cannot be evicted
 */
struct Frame {
    int     page_id    = -1;   // -1 means empty frame
    int     usage_count = 0;   // 0 to 5 (capped at 5)
    bool    pinned     = false;
    
    Frame() = default;
};

/**
 * BufferPool: Implements ClockSweep page replacement algorithm
 * 
 * ClockSweep is PostgreSQL's eviction strategy. It approximates LRU
 * without the overhead of maintaining an ordered list.
 * 
 * Algorithm:
 * 1. Clock hand sweeps through frames circularly
 * 2. If frame has usage_count > 0: decrement and move on (second chance)
 * 3. If frame has usage_count == 0 and not pinned: evict this frame
 * 4. Pinned frames are never evicted
 */
class BufferPool {
private:
    std::vector<Frame>              frames;        // Buffer pool frames
    std::unordered_map<int, int>    page_to_frame; // page_id -> frame index
    int                             hand;          // Clock hand position
    int                             capacity;      // Total number of frames
    
    // Statistics
    int hits   = 0;
    int misses = 0;
    int evictions = 0;

public:
    explicit BufferPool(int cap) : frames(cap), hand(0), capacity(cap) {}
    
    /**
     * fetch: Load a page into the buffer pool
     * Returns frame index on success, -1 if all frames are pinned
     */
    int fetch(int page_id) {
        // Check if page is already in buffer pool
        auto it = page_to_frame.find(page_id);
        if (it != page_to_frame.end()) {
            // HIT: Page found in buffer pool
            int idx = it->second;
            frames[idx].usage_count = std::min(frames[idx].usage_count + 1, 5);
            hits++;
            
            std::cout << "[HIT]  page " << std::setw(3) << page_id
                      << " in frame " << idx
                      << " (usage=" << frames[idx].usage_count << ")" << std::endl;
            return idx;
        }
        
        // MISS: Page not in buffer pool, need to find victim
        misses++;
        int victim = clocksweep();
        
        if (victim == -1) {
            std::cerr << "[ERROR] All frames pinned, cannot evict!" << std::endl;
            return -1;
        }
        
        // Evict current occupant if frame is not empty
        if (frames[victim].page_id != -1) {
            std::cout << "[EVICT] page " << std::setw(3) << frames[victim].page_id
                      << " from frame " << victim << std::endl;
            page_to_frame.erase(frames[victim].page_id);
            evictions++;
        }
        
        // Load new page into victim frame
        frames[victim] = {page_id, 1, false};
        page_to_frame[page_id] = victim;
        
        std::cout << "[MISS] page " << std::setw(3) << page_id
                  << " loaded into frame " << victim << std::endl;
        
        return victim;
    }
    
    /**
     * pin: Mark a page as pinned (cannot be evicted)
     */
    void pin(int page_id) {
        auto it = page_to_frame.find(page_id);
        if (it != page_to_frame.end()) {
            frames[it->second].pinned = true;
            std::cout << "[PIN]  page " << page_id << std::endl;
        }
    }
    
    /**
     * unpin: Remove pin from a page (can be evicted now)
     */
    void unpin(int page_id) {
        auto it = page_to_frame.find(page_id);
        if (it != page_to_frame.end()) {
            frames[it->second].pinned = false;
            std::cout << "[UNPIN] page " << page_id << std::endl;
        }
    }

    /**
     * print_state: Display current buffer pool state
     */
    void print_state() const {
        std::cout << "\n┌─────────────────────────────────────────────────────┐" << std::endl;
        std::cout << "│ Buffer Pool State (hand=" << hand << ")                      │" << std::endl;
        std::cout << "├────────┬─────────┬────────┬────────┬──────────────┤" << std::endl;
        std::cout << "│ Frame  │ Page ID │ Usage  │ Pinned │ Hand         │" << std::endl;
        std::cout << "├────────┼─────────┼────────┼────────┼──────────────┤" << std::endl;
        
        for (int i = 0; i < capacity; i++) {
            const auto& f = frames[i];
            std::cout << "│   " << std::setw(2) << i << "   │  ";
            
            if (f.page_id == -1) {
                std::cout << " --  ";
            } else {
                std::cout << std::setw(3) << f.page_id << "  ";
            }
            
            std::cout << " │   " << f.usage_count << "    │   "
                      << (f.pinned ? "YES" : " NO") << "  │";
            
            if (i == hand) {
                std::cout << "  <-- HERE";
            }
            
            std::cout << std::endl;
        }
        
        std::cout << "└────────┴─────────┴────────┴────────┴──────────────┘" << std::endl;
        std::cout << "Stats: Hits=" << hits << " Misses=" << misses 
                  << " Evictions=" << evictions 
                  << " Hit Rate=" << std::fixed << std::setprecision(2) 
                  << (hits + misses > 0 ? 100.0 * hits / (hits + misses) : 0.0) 
                  << "%\n" << std::endl;
    }

private:
    /**
     * clocksweep: Find a victim frame using clock algorithm
     * Returns frame index to evict, or -1 if all frames are pinned
     */
    int clocksweep() {
        int checked = 0;
        int max_checks = 2 * capacity;  // Two full sweeps maximum
        
        while (checked < max_checks) {
            Frame& f = frames[hand];
            
            // Skip pinned frames
            if (!f.pinned) {
                if (f.usage_count == 0) {
                    // Found victim! Frame with usage_count = 0
                    int victim = hand;
                    hand = (hand + 1) % capacity;
                    return victim;
                }
                // Give second chance: decrement usage_count
                f.usage_count--;
            }
            
            // Move clock hand forward (circular)
            hand = (hand + 1) % capacity;
            checked++;
        }
        
        // All frames are pinned or have high usage counts
        return -1;
    }
};


/**
 * Test scenarios demonstrating ClockSweep algorithm behavior
 */
int main() {
    std::cout << "=== Lab 3: ClockSweep Buffer Pool Algorithm ===" << std::endl;
    std::cout << "PostgreSQL's page replacement strategy\n" << std::endl;
    
    // Test 1: Basic access pattern from lab requirements
    std::cout << "┌────────────────────────────────────────────────────┐" << std::endl;
    std::cout << "│ Test 1: Basic Access Pattern                       │" << std::endl;
    std::cout << "│ Pool Size: 4 frames                                │" << std::endl;
    std::cout << "│ Access: 1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5        │" << std::endl;
    std::cout << "└────────────────────────────────────────────────────┘\n" << std::endl;
    
    BufferPool pool1(4);
    std::vector<int> accesses1 = {1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5};
    
    for (int page : accesses1) {
        pool1.fetch(page);
    }
    pool1.print_state();
    
    // Test 2: Sequential scan behavior
    std::cout << "\n┌────────────────────────────────────────────────────┐" << std::endl;
    std::cout << "│ Test 2: Sequential Scan (Large Range)              │" << std::endl;
    std::cout << "│ Pool Size: 3 frames                                │" << std::endl;
    std::cout << "│ Access: 10, 11, 12, 13, 14, 15, 16, 17, 18, 19    │" << std::endl;
    std::cout << "└────────────────────────────────────────────────────┘\n" << std::endl;
    
    BufferPool pool2(3);
    for (int page = 10; page <= 19; page++) {
        pool2.fetch(page);
    }
    pool2.print_state();
    
    // Test 3: Hot page protection (repeated access)
    std::cout << "\n┌────────────────────────────────────────────────────┐" << std::endl;
    std::cout << "│ Test 3: Hot Page Protection                        │" << std::endl;
    std::cout << "│ Pool Size: 3 frames                                │" << std::endl;
    std::cout << "│ Page 100 accessed repeatedly (hot page)            │" << std::endl;
    std::cout << "└────────────────────────────────────────────────────┘\n" << std::endl;
    
    BufferPool pool3(3);
    pool3.fetch(100);
    pool3.fetch(100);  // Increase usage_count
    pool3.fetch(100);  // Increase usage_count
    pool3.fetch(100);  // Increase usage_count (now at 4)
    pool3.fetch(101);
    pool3.fetch(102);
    pool3.fetch(103);  // Should evict 101 (usage=1), not 100 (usage=4)
    pool3.fetch(104);  // Should evict 102, not 100
    pool3.print_state();
    
    // Test 4: Pinned page behavior
    std::cout << "\n┌────────────────────────────────────────────────────┐" << std::endl;
    std::cout << "│ Test 4: Pinned Page (Cannot Evict)                 │" << std::endl;
    std::cout << "│ Pool Size: 3 frames                                │" << std::endl;
    std::cout << "│ Page 200 is pinned and cannot be evicted           │" << std::endl;
    std::cout << "└────────────────────────────────────────────────────┘\n" << std::endl;
    
    BufferPool pool4(3);
    pool4.fetch(200);
    pool4.pin(200);    // Pin page 200
    pool4.fetch(201);
    pool4.fetch(202);
    pool4.fetch(203);  // Should evict 201 or 202, NOT 200
    pool4.fetch(204);
    pool4.print_state();
    
    std::cout << "Note: Page 200 remains in buffer (pinned)" << std::endl;
    pool4.unpin(200);
    std::cout << "\nAfter unpinning page 200:" << std::endl;
    pool4.fetch(205);  // Now 200 can be evicted
    pool4.print_state();
    
    // Test 5: Usage count demonstration
    std::cout << "\n┌────────────────────────────────────────────────────┐" << std::endl;
    std::cout << "│ Test 5: Usage Count Cap at 5                       │" << std::endl;
    std::cout << "│ Access page 300 many times                         │" << std::endl;
    std::cout << "└────────────────────────────────────────────────────┘\n" << std::endl;
    
    BufferPool pool5(2);
    pool5.fetch(300);
    for (int i = 0; i < 10; i++) {
        pool5.fetch(300);  // usage_count capped at 5
    }
    pool5.print_state();
    std::cout << "Note: usage_count capped at 5 (PostgreSQL behavior)" << std::endl;
    
    std::cout << "\n=== All Tests Complete ===" << std::endl;
    
    return 0;
}
