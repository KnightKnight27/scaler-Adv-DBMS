#ifndef CLOCKSWEEP_H
#define CLOCKSWEEP_H

#include <iostream>
#include <vector>
#include <unordered_map>
#include <cstddef>

template <typename KeyType>
class ClockSweep {
private:
    struct FrameSlot {
        KeyType key;
        bool is_referenced = false;
    };

public:
    explicit ClockSweep(const int buffer_capacity)
        : capacity_(buffer_capacity), clock_hand_(0), hit_count_(0),
          miss_count_(0), eviction_count_(0) {
        frames_.reserve(capacity_);
    }

    ~ClockSweep() = default;

    // Look up a page in the buffer; updates hit/miss stats and reference bit on hit
    bool getKey(const KeyType& key) {
        auto found = page_to_frame_map_.find(key);
        if (found != page_to_frame_map_.end()) {
            frames_[found->second].is_referenced = true;
            hit_count_++;
            std::cout << "HIT  -> page " << key << "\n";
            return true;
        }
        miss_count_++;
        std::cout << "MISS -> page " << key << "\n";
        return false;
    }

    // Load a page into the buffer; evicts a victim via clock sweep if full
    void putKey(const KeyType& key) {
        auto found = page_to_frame_map_.find(key);
        if (found != page_to_frame_map_.end()) {
            frames_[found->second].is_referenced = true;
            return;
        }

        if (static_cast<int>(frames_.size()) < capacity_) {
            FrameSlot new_frame;
            new_frame.key = key;
            new_frame.is_referenced = true;
            frames_.push_back(new_frame);
            page_to_frame_map_[key] = static_cast<int>(frames_.size() - 1);
            std::cout << "       (loaded into empty frame " << frames_.size() - 1 << ")\n";
            return;
        }

        const int victim_frame_index = chooseVictimFrame();
        std::cout << "       (evicted page " << frames_[victim_frame_index].key
                  << " from frame " << victim_frame_index << ")\n";

        page_to_frame_map_.erase(frames_[victim_frame_index].key);
        frames_[victim_frame_index].key = key;
        frames_[victim_frame_index].is_referenced = true;
        page_to_frame_map_[key] = victim_frame_index;

        clock_hand_ = (victim_frame_index + 1) % capacity_;
    }

    // Display current state of the buffer frames
    void printBuffer() const {
        std::cout << "\n  [Buffer]\n";
        for (int frame_index = 0; frame_index < static_cast<int>(frames_.size()); ++frame_index) {
            std::cout << "  frame " << frame_index
                      << " | page=" << frames_[frame_index].key
                      << " | bit=" << frames_[frame_index].is_referenced
                      << (frame_index == clock_hand_ ? "  <- clock" : "")
                      << "\n";
        }
        std::cout << "\n";
    }

    // Display cache statistics
    void printStats() const {
        const int total_accesses = hit_count_ + miss_count_;
        std::cout << "\n--- Stats ---\n";
        std::cout << "hits      : " << hit_count_ << "\n";
        std::cout << "misses    : " << miss_count_ << "\n";
        std::cout << "evictions : " << eviction_count_ << "\n";
        std::cout << "hit ratio : " << (total_accesses == 0 ? 0.0 : (100.0 * hit_count_ / total_accesses)) << "%\n";
    }

private:
    // Buffer management state
    const int capacity_;
    std::vector<FrameSlot> frames_;
    std::unordered_map<KeyType, int> page_to_frame_map_;
    int clock_hand_;

    // Cache statistics
    int hit_count_;
    int miss_count_;
    int eviction_count_;

    // Find and return the index of the victim frame using clock sweep algorithm
    int chooseVictimFrame() {
        while (true) {
            if (frames_[clock_hand_].is_referenced) {
                frames_[clock_hand_].is_referenced = false;
                clock_hand_ = (clock_hand_ + 1) % capacity_;
            } else {
                eviction_count_++;
                return clock_hand_;
            }
        }
    }
};

#endif
