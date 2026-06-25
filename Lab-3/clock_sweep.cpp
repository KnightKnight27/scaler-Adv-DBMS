#include <iostream>
#include <vector>
#include <unordered_map>
#include <iomanip>
#include <string>

// Maximum usage count limit, matching PostgreSQL's BM_MAX_USAGE_COUNT = 5
constexpr int MAX_USAGE_COUNT = 5;

// Alias for database page/block identifiers
using PageId = int;
constexpr PageId INVALID_PAGE_ID = -1;

/**
 * @brief Represents a buffer header (metadata frame) in the buffer pool.
 * Matches PostgreSQL's internal buffer descriptor structure.
 */
struct BufferHeader {
    int frame_id = -1;             // Physical frame index in the buffer pool
    PageId page_id = INVALID_PAGE_ID; // ID of the disk page currently loaded (tag)
    int pin_count = 0;            // Number of concurrent transactions pinning the page (refcount)
    int usage_count = 0;          // Popularity counter (0 to 5), representing frequency of access
    bool is_dirty = false;         // Set to true if the page was modified and must be flushed to disk
};

/**
 * @brief Manages the buffer pool and executes PostgreSQL's Clock-Sweep eviction algorithm.
 */
class ClockSweepBufferManager {
private:
    size_t pool_size;
    std::vector<BufferHeader> pool;              // The buffer pool array
    std::unordered_map<PageId, int> page_table;  // Hash table: maps PageId to Frame ID
    size_t clock_hand = 0;                       // Circular pointer (the clock hand)

    /**
     * @brief Circularly increments the clock hand.
     */
    void advance_hand() {
        clock_hand = (clock_hand + 1) % pool_size;
    }

    /**
     * @brief Implements PostgreSQL's StrategyGetBuffer (Clock-Sweep Eviction).
     * Sweeps the buffer descriptors to find a victim page with pin_count == 0 and usage_count == 0.
     * Decrements usage_count of unpinned pages as the hand passes over them.
     * 
     * @return The frame ID selected for eviction, or -1 if all pages are pinned.
     */
    int find_victim() {
        size_t total_scanned = 0;
        // In the worst case, to decrement all usage counts from 5 to 0 in a pool of size N,
        // we need at most 5 * N sweeps. We cap the search to prevent infinite loops if all are pinned.
        size_t max_scans = pool_size * (MAX_USAGE_COUNT + 1);

        std::cout << "[CLOCK SWEEP] Initiating eviction sweep. Hand starting at Frame " << clock_hand << "...\n";

        while (total_scanned < max_scans) {
            BufferHeader& candidate = pool[clock_hand];

            // Rule 1: A page cannot be evicted if it is currently pinned (pin_count > 0)
            if (candidate.pin_count > 0) {
                std::cout << "  -> Frame " << clock_hand << " (Page " << candidate.page_id 
                          << "): Pinned [pin_count = " << candidate.pin_count 
                          << "]. Skipping and advancing hand.\n";
            } 
            // Rule 2: If the page is unpinned (pin_count == 0)
            else {
                // If usage_count is > 0, decrement it to give it a "second chance"
                if (candidate.usage_count > 0) {
                    candidate.usage_count--;
                    std::cout << "  -> Frame " << clock_hand << " (Page " << candidate.page_id 
                              << "): Unpinned but popular. Decremented usage_count to " 
                              << candidate.usage_count << ". Advancing hand.\n";
                } 
                // If usage_count is 0, we have found our victim!
                else {
                    int victim_frame = static_cast<int>(clock_hand);
                    std::cout << "  >> SUCCESS: Frame " << victim_frame 
                              << " selected as victim (usage_count = 0, pin_count = 0).\n";
                    
                    // PostgreSQL advances the clock hand past the victim before returning
                    advance_hand();
                    return victim_frame;
                }
            }

            advance_hand();
            total_scanned++;
        }

        return -1; // All pages are pinned, buffer pool is completely full
    }

public:
    explicit ClockSweepBufferManager(size_t size) : pool_size(size), pool(size) {
        for (size_t i = 0; i < pool_size; ++i) {
            pool[i].frame_id = static_cast<int>(i);
        }
    }

    /**
     * @brief Pins a page. If the page is in the buffer pool, its pin count and usage count are incremented.
     * If the page is not in the pool, a victim page is evicted using Clock-Sweep, and the new page is loaded.
     * 
     * @param page_id The database page ID to access.
     * @return The frame ID where the page is loaded.
     */
    int pin_page(PageId page_id) {
        std::cout << ">>> Pin Request: Page " << page_id << "\n";

        // Case 1: Page table hit (Page already in buffer pool)
        if (page_table.find(page_id) != page_table.end()) {
            int frame_id = page_table[page_id];
            pool[frame_id].pin_count++;
            
            // Increment popularity up to PostgreSQL's limit
            if (pool[frame_id].usage_count < MAX_USAGE_COUNT) {
                pool[frame_id].usage_count++;
            }
            std::cout << "[HIT] Page " << page_id << " already in Frame " << frame_id 
                      << " (pin_count = " << pool[frame_id].pin_count 
                      << ", usage_count = " << pool[frame_id].usage_count << ")\n\n";
            return frame_id;
        }

        // Case 2: Page table miss (Page must be loaded from disk)
        int victim_frame = find_victim();
        if (victim_frame == -1) {
            std::cerr << "[OUT OF MEMORY ERROR] Failed to pin Page " << page_id 
                      << ": Buffer pool is completely pinned!\n\n";
            return -1;
        }

        BufferHeader& victim = pool[victim_frame];

        // If the frame contains an existing page, perform eviction cleanup
        if (victim.page_id != INVALID_PAGE_ID) {
            std::cout << "[EVICTION] Evicting Page " << victim.page_id 
                      << " from Frame " << victim_frame;
            if (victim.is_dirty) {
                std::cout << " (Flushing DIRTY changes to disk...)";
            }
            std::cout << "\n";
            page_table.erase(victim.page_id);
        }

        // Load new page into the victim frame
        victim.page_id = page_id;
        victim.pin_count = 1;
        victim.usage_count = 1; // Initially set to 1 on read
        victim.is_dirty = false;
        page_table[page_id] = victim_frame;

        std::cout << "[MISS] Loaded Page " << page_id << " into Frame " << victim_frame << "\n\n";
        return victim_frame;
    }

    /**
     * @brief Unpins a page, releasing the lock. Decrements its pin count.
     * Optionally marks the page as dirty if changes were made.
     */
    void unpin_page(PageId page_id, bool is_dirty) {
        if (page_table.find(page_id) == page_table.end()) {
            std::cerr << "[WARNING] Unpin failed: Page " << page_id << " is not in the buffer pool.\n\n";
            return;
        }

        int frame_id = page_table[page_id];
        BufferHeader& frame = pool[frame_id];

        if (frame.pin_count <= 0) {
            std::cerr << "[WARNING] Unpin failed: Page " << page_id << " in Frame " << frame_id 
                      << " is already at 0 pins.\n\n";
            return;
        }

        frame.pin_count--;
        if (is_dirty) {
            frame.is_dirty = true;
        }

        std::cout << "[UNPIN] Page " << page_id << " from Frame " << frame_id 
                  << " (Remaining pins = " << frame.pin_count 
                  << ", is_dirty = " << (frame.is_dirty ? "true" : "false") << ")\n\n";
    }

    /**
     * @brief Prints a visual state of the buffer pool.
     */
    void print_state() const {
        std::cout << "================================ BUFFER POOL STATE ================================\n";
        std::cout << std::left << std::setw(10) << "Frame ID" 
                  << std::setw(12) << "Page ID" 
                  << std::setw(12) << "Pin Count" 
                  << std::setw(15) << "Usage Count" 
                  << std::setw(10) << "Dirty?" 
                  << "Clock Hand Location\n";
        std::cout << "-----------------------------------------------------------------------------------\n";
        for (size_t i = 0; i < pool_size; ++i) {
            const auto& frame = pool[i];
            std::string page_str = (frame.page_id == INVALID_PAGE_ID) ? "EMPTY" : std::to_string(frame.page_id);
            std::string hand_str = (clock_hand == i) ? " <--- (CLOCK HAND)" : "";
            std::cout << std::left << std::setw(10) << frame.frame_id 
                      << std::setw(12) << page_str 
                      << std::setw(12) << frame.pin_count 
                      << std::setw(15) << frame.usage_count 
                      << std::setw(10) << (frame.is_dirty ? "YES" : "NO") 
                      << hand_str << "\n";
        }
        std::cout << "===================================================================================\n\n";
    }
};

int main() {
    std::cout << "---------------------------------------------------------\n";
    std::cout << "   PostgreSQL Clock-Sweep Eviction Algorithm Simulator   \n";
    std::cout << "---------------------------------------------------------\n\n";

    // Initialize a small buffer pool of size 3 for easy trace mapping
    ClockSweepBufferManager db_buffer_pool(3);
    db_buffer_pool.print_state();

    // 1. Load three pages into the empty buffer pool (Misses)
    db_buffer_pool.pin_page(101);
    db_buffer_pool.pin_page(102);
    db_buffer_pool.pin_page(103);
    db_buffer_pool.print_state();

    // 2. Unpin pages to make them candidates for eviction
    // Page 101: pin_count=0, usage_count=1
    // Page 102: pin_count=0, usage_count=1 (mark dirty)
    // Page 103: Keep pinned (pin_count=1, usage_count=1)
    db_buffer_pool.unpin_page(101, false);
    db_buffer_pool.unpin_page(102, true); 
    db_buffer_pool.print_state();

    // 3. Pin Page 101 again to increase its usage count (Hit)
    // Page 101 becomes popular: pin_count=1, usage_count=2
    db_buffer_pool.pin_page(101);
    db_buffer_pool.unpin_page(101, false); // pin_count=0, usage_count=2
    db_buffer_pool.print_state();

    // 4. Request Page 104 (Miss - Eviction required)
    // We expect:
    // - Clock hand is currently at Frame 0 (Page 101).
    // - Frame 0 has pin_count=0, usage_count=2. Sweeper decrements usage_count to 1 and advances to Frame 1.
    // - Frame 1 (Page 102) has pin_count=0, usage_count=1. Sweeper decrements usage_count to 0 and advances to Frame 2.
    // - Frame 2 (Page 103) has pin_count=1. Sweeper skips it (pinned) and advances to Frame 0.
    // - Frame 0 (Page 101) now has usage_count=1. Sweeper decrements it to 0 and advances to Frame 1.
    // - Frame 1 (Page 102) now has usage_count=0. Sweeper selects it as the victim!
    // - Frame 1 is dirty, so it is flushed. Page 104 is loaded into Frame 1.
    // - Clock hand moves to Frame 2.
    db_buffer_pool.pin_page(104);
    db_buffer_pool.print_state();

    // 5. Unpin 103 (now candidate) and Pin 105 (triggers sweep)
    db_buffer_pool.unpin_page(103, false); // pin_count=0, usage_count=1
    db_buffer_pool.unpin_page(104, false); // pin_count=0, usage_count=1
    
    // Requesting Page 105
    // - Clock hand starts at Frame 2 (Page 103).
    // - Frame 2: pin_count=0, usage_count=1. Decremented to 0. Clock hand advances to Frame 0.
    // - Frame 0: Page 101 has pin_count=0, usage_count=0. Selected as victim!
    // - Page 105 loaded into Frame 0. Clock hand moves to Frame 1.
    db_buffer_pool.pin_page(105);
    db_buffer_pool.print_state();

    return 0;
}
