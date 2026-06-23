#include <iostream>
#include <vector>
#include <unordered_map>
#include <string>

// Represents a 8KB database page
struct Page {
    int page_id = -1;
    bool is_dirty = false;
    int pin_count = 0;
    bool ref_bit = false;
    std::string data;

    void Reset() {
        page_id = -1;
        is_dirty = false;
        pin_count = 0;
        ref_bit = false;
        data.clear();
    }
};

class ClockSweepBufferPool {
private:
    std::vector<Page> frames;
    size_t pool_size;
    size_t clock_hand;
    
    // Maps page_id -> frame_index
    std::unordered_map<int, size_t> page_table;

    void FlushPageToDisk(int page_id, const std::string& data) {
        std::cout << "  [Disk I/O] Flushing dirty page " << page_id << " to disk." << std::endl;
    }

    std::string ReadPageFromDisk(int page_id) {
        std::cout << "  [Disk I/O] Reading page " << page_id << " from disk." << std::endl;
        return "Data for page " + std::to_string(page_id);
    }

    // The core ClockSweep eviction algorithm
    size_t EvictPage() {
        while (true) {
            Page& frame = frames[clock_hand];

            // If page is pinned, we can't evict it
            if (frame.pin_count > 0) {
                clock_hand = (clock_hand + 1) % pool_size;
                continue;
            }

            // If reference bit is true, give it a second chance (set to false)
            if (frame.ref_bit) {
                frame.ref_bit = false;
                std::cout << "  [Clock] Sweeping past frame " << clock_hand << " (Page " << frame.page_id << "), clearing ref_bit." << std::endl;
                clock_hand = (clock_hand + 1) % pool_size;
            } else {
                // We found a victim!
                if (frame.is_dirty) {
                    FlushPageToDisk(frame.page_id, frame.data);
                }
                std::cout << "  [Clock] Evicting Page " << frame.page_id << " from frame " << clock_hand << "." << std::endl;
                
                page_table.erase(frame.page_id);
                size_t victim_frame = clock_hand;
                
                // Move clock hand forward for the next time
                clock_hand = (clock_hand + 1) % pool_size;
                return victim_frame;
            }
        }
    }

public:
    ClockSweepBufferPool(size_t size) : pool_size(size), clock_hand(0) {
        frames.resize(size);
    }

    Page* FetchPage(int page_id) {
        std::cout << "Fetching Page " << page_id << "..." << std::endl;
        
        // 1. Check if page is already in the buffer pool
        if (page_table.find(page_id) != page_table.end()) {
            size_t frame_idx = page_table[page_id];
            frames[frame_idx].pin_count++;
            frames[frame_idx].ref_bit = true; // Set reference bit for clock sweep
            std::cout << "  -> Cache Hit in frame " << frame_idx << std::endl;
            return &frames[frame_idx];
        }

        // 2. Page fault! Need to find a free frame or evict an existing one
        std::cout << "  -> Cache Miss!" << std::endl;
        size_t target_frame = -1;

        // Try to find an empty frame first
        for (size_t i = 0; i < pool_size; ++i) {
            if (frames[i].page_id == -1) {
                target_frame = i;
                break;
            }
        }

        // If no empty frame, invoke ClockSweep eviction
        if (target_frame == -1) {
            target_frame = EvictPage();
        }

        // 3. Load page from disk into the target frame
        frames[target_frame].Reset();
        frames[target_frame].page_id = page_id;
        frames[target_frame].data = ReadPageFromDisk(page_id);
        frames[target_frame].pin_count = 1;
        frames[target_frame].ref_bit = true;
        
        page_table[page_id] = target_frame;
        std::cout << "  -> Loaded Page " << page_id << " into frame " << target_frame << std::endl;

        return &frames[target_frame];
    }

    void UnpinPage(int page_id, bool is_dirty) {
        if (page_table.find(page_id) == page_table.end()) return;
        
        size_t frame_idx = page_table[page_id];
        if (frames[frame_idx].pin_count > 0) {
            frames[frame_idx].pin_count--;
        }
        if (is_dirty) {
            frames[frame_idx].is_dirty = true;
        }
        std::cout << "Unpinned Page " << page_id << " (Dirty: " << (is_dirty ? "Yes" : "No") << ", Pins left: " << frames[frame_idx].pin_count << ")" << std::endl;
    }
};

int main() {
    std::cout << "--- Initializing Buffer Pool (Size: 3) ---" << std::endl;
    ClockSweepBufferPool pool(3);

    pool.FetchPage(1); pool.UnpinPage(1, false);
    pool.FetchPage(2); pool.UnpinPage(2, true);  // Dirty page
    pool.FetchPage(3); pool.UnpinPage(3, false);

    std::cout << "\n--- Buffer Pool Full. Next fetch will cause eviction ---" << std::endl;
    // Page 1, 2, 3 have ref_bit=true. Clock will sweep past them, setting to false.
    // Page 1 will be evicted (it's not dirty).
    pool.FetchPage(4); pool.UnpinPage(4, false);

    std::cout << "\n--- Evicting a Dirty Page ---" << std::endl;
    // Page 2 is dirty. It should be written to disk during eviction.
    pool.FetchPage(5); pool.UnpinPage(5, false);

    std::cout << "\n--- Pinning a page prevents eviction ---" << std::endl;
    pool.FetchPage(3); // Cache hit, pinned (pin_count=1)
    
    // We try to fetch page 6. 
    // Frame 0: Page 4 (ref=false, unpinned)
    // Frame 1: Page 5 (ref=false, unpinned)
    // Frame 2: Page 3 (ref=true, pinned)
    // Page 4 should be evicted.
    pool.FetchPage(6); pool.UnpinPage(6, false);

    pool.UnpinPage(3, false); // Finally unpin page 3

    return 0;
}
