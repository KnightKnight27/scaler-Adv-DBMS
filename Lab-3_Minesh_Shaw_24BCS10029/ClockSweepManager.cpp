#include "ClockSweepManager.hpp"

ClockSweepManager::ClockSweepManager(size_t size) 
    : pool_size(size), frames(size), clock_hand(0) {}

void ClockSweepManager::AccessPage(int page_id) {
    std::cout << "\n[Access] Requesting Page " << page_id << "\n";
    
    auto it = page_table.find(page_id);
    
    if (it != page_table.end()) {
        size_t frame_id = it->second;
        frames[frame_id].usage_count = std::min(5, frames[frame_id].usage_count + 1);
        std::cout << "  -> Page hit in frame " << frame_id 
                  << ". Usage count bumped to " << frames[frame_id].usage_count << "\n";
        return;
    }

    std::cout << "  -> Page miss. Clock hand starts at frame " << clock_hand << ".\n";
    size_t victim_frame = FindVictim();

    if (frames[victim_frame].is_valid) {
        std::cout << "  -> Evicting Page " << frames[victim_frame].page_id 
                  << " from frame " << victim_frame << ".\n";
        page_table.erase(frames[victim_frame].page_id);
    }

    frames[victim_frame].page_id = page_id;
    frames[victim_frame].usage_count = 1;
    frames[victim_frame].pin_count = 0;
    frames[victim_frame].is_valid = true;

    page_table[page_id] = victim_frame;
    std::cout << "  -> Loaded Page " << page_id << " into frame " << victim_frame << ".\n";
}

size_t ClockSweepManager::FindVictim() {
    size_t iterations = 0;
    
    while (true) {
        BufferFrame& frame = frames[clock_hand];

        if (frame.pin_count > 0) {
            std::cout << "    [Sweep] Frame " << clock_hand << " pinned. Skipping.\n";
        } 
        else if (frame.usage_count > 0) {
            frame.usage_count--;
            std::cout << "    [Sweep] Frame " << clock_hand << " given a second chance. Usage decremented to " 
                      << frame.usage_count << ".\n";
        } 
        else {
            size_t victim = clock_hand;
            std::cout << "    [Sweep] Frame " << clock_hand << " selected as victim.\n";
            
            clock_hand = (clock_hand + 1) % pool_size;
            return victim;
        }

        clock_hand = (clock_hand + 1) % pool_size;
        iterations++;

        if (iterations > pool_size * 6) {
            throw std::runtime_error("Buffer pool deadlock: All frames are pinned.");
        }
    }
}

void ClockSweepManager::PinPage(int page_id) {
    auto it = page_table.find(page_id);
    if (it != page_table.end()) {
        frames[it->second].pin_count++;
        std::cout << "[Pin] Page " << page_id << " pinned. (Count: " 
                  << frames[it->second].pin_count << ")\n";
    }
}

void ClockSweepManager::UnpinPage(int page_id) {
    auto it = page_table.find(page_id);
    if (it != page_table.end() && frames[it->second].pin_count > 0) {
        frames[it->second].pin_count--;
        std::cout << "[Unpin] Page " << page_id << " unpinned. (Count: " 
                  << frames[it->second].pin_count << ")\n";
    }
}

void ClockSweepManager::PrintState() const {
    std::cout << "\n=== Buffer Pool State (Hand @ " << clock_hand << ") ===\n";
    std::cout << std::left << std::setw(8) << "Frame" 
              << std::setw(10) << "Page ID" 
              << std::setw(10) << "Usage" 
              << std::setw(10) << "Pins" << "\n";
    std::cout << "--------------------------------------\n";
    
    for (size_t i = 0; i < pool_size; i++) {
        std::cout << std::left << std::setw(8) << i;
        if (frames[i].is_valid) {
            std::cout << std::setw(10) << frames[i].page_id 
                      << std::setw(10) << frames[i].usage_count 
                      << std::setw(10) << frames[i].pin_count << "\n";
        } else {
            std::cout << std::setw(10) << "EMPTY" 
                      << std::setw(10) << "-" 
                      << std::setw(10) << "-" << "\n";
        }
    }
    std::cout << "======================================\n";
}