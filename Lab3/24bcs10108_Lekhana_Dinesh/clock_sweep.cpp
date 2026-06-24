#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <stdexcept>

// Clock Sweep page replacement / buffer eviction algorithm.
// Uses fixed-size buffer frames and a circular "clock hand" to give
// pages a second chance before eviction.

template <typename Key, typename Value>
class ClockSweep {
public:
    explicit ClockSweep(size_t capacity)
        : capacity_(capacity), frames_(capacity), clock_hand_(0) {
        if (capacity_ == 0) {
            throw std::invalid_argument("Capacity must be greater than zero.");
        }
    }

    // Insert or update a page in the buffer.
    void put(const Key& key, const Value& value) {
        auto it = index_map_.find(key);
        if (it != index_map_.end()) {
            // Update existing page and mark it as referenced.
            size_t frame_index = it->second;
            frames_[frame_index].key = key;
            frames_[frame_index].value = value;
            frames_[frame_index].referenced = true;
            frames_[frame_index].valid = true;
            std::cout << "Update existing page: " << key << " with new value."
                      << " [Second chance granted]" << std::endl;
            return;
        }

        // If there is a free frame, use it directly.
        for (size_t i = 0; i < capacity_; ++i) {
            if (!frames_[i].valid) {
                frames_[i].key = key;
                frames_[i].value = value;
                frames_[i].valid = true;
                frames_[i].referenced = true;
                index_map_[key] = i;
                std::cout << "Insert page " << key << " into free frame " << i << "."
                          << std::endl;
                return;
            }
        }

        // Buffer is full, evict using Clock Sweep.
        std::cout << "Buffer full, searching for eviction victim for page "
                  << key << "." << std::endl;
        evictAndReplace(key, value);
    }

    // Get a page value by key. Returns std::optional<Value> to indicate hit or miss.
    std::optional<Value> get(const Key& key) {
        auto it = index_map_.find(key);
        if (it == index_map_.end()) {
            std::cout << "Page miss: " << key << "." << std::endl;
            return std::nullopt;
        }

        size_t frame_index = it->second;
        frames_[frame_index].referenced = true;
        std::cout << "Page hit: " << key << " found in frame " << frame_index
                  << ". Reference bit set to true." << std::endl;
        return frames_[frame_index].value;
    }

    bool contains(const Key& key) const {
        return index_map_.find(key) != index_map_.end();
    }

    void display() const {
        std::cout << "\nCurrent buffer state:" << std::endl;
        std::cout << "Index | Key | Value | Valid | Referenced" << std::endl;
        std::cout << "----------------------------------------" << std::endl;
        for (size_t i = 0; i < capacity_; ++i) {
            const auto& frame = frames_[i];
            std::cout << i << "     | ";
            if (frame.valid) {
                std::cout << frame.key << " | " << frame.value << " | true  | "
                          << (frame.referenced ? "true" : "false") << std::endl;
            } else {
                std::cout << "-   | -     | false | false" << std::endl;
            }
        }
        std::cout << std::endl;
    }

private:
    struct Frame {
        Key key{};
        Value value{};
        bool valid = false;
        bool referenced = false;
    };

    void evictAndReplace(const Key& key, const Value& value) {
        while (true) {
            Frame& candidate = frames_[clock_hand_];

            if (!candidate.valid) {
                // Should not happen if the buffer is truly full.
                insertAtClockHand(key, value);
                return;
            }

            if (candidate.referenced) {
                std::cout << "Frame " << clock_hand_ << " has reference bit = true."
                          << " Giving second chance and clearing bit." << std::endl;
                candidate.referenced = false;
                advanceClock();
            } else {
                std::cout << "Evicting page " << candidate.key << " from frame "
                          << clock_hand_ << "." << std::endl;
                index_map_.erase(candidate.key);
                insertAtClockHand(key, value);
                return;
            }
        }
    }

    void insertAtClockHand(const Key& key, const Value& value) {
        frames_[clock_hand_].key = key;
        frames_[clock_hand_].value = value;
        frames_[clock_hand_].valid = true;
        frames_[clock_hand_].referenced = true;
        index_map_[key] = clock_hand_;
        std::cout << "Insert page " << key << " into frame " << clock_hand_
                  << " after eviction/clock advance." << std::endl;
        advanceClock();
    }

    void advanceClock() {
        clock_hand_ = (clock_hand_ + 1) % capacity_;
    }

    size_t capacity_;
    std::vector<Frame> frames_;
    std::unordered_map<Key, size_t> index_map_;
    size_t clock_hand_;
};

int main() {
    std::cout << "Lab 3: Clock Sweep Page Replacement Demo" << std::endl;
    ClockSweep<int, std::string> cache(3);

    cache.put(1, "PageA");
    cache.put(2, "PageB");
    cache.put(3, "PageC");
    cache.display();

    cache.get(1); // hit
    cache.get(2); // hit
    cache.display();

    cache.put(4, "PageD"); // eviction should occur when adding 4
    cache.display();
    cache.get(1); // miss because page 1 was evicted
    cache.get(3); // hit if still present
    cache.get(2); // hit if still present
    cache.put(5, "PageE");
    cache.display();

    std::cout << "Demo complete. Final cache state shown above." << std::endl;
    return 0;
}
