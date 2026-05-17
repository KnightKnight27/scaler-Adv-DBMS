#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <stdexcept>

using namespace std;

// Clock Sweep Page Replacement Algorithm
template <typename Key, typename Value>
class ClockSweep {
public:
    explicit ClockSweep(size_t capacity)
        : capacity_(capacity),
          frames_(capacity),
          clock_hand_(0) {

        if (capacity_ == 0) {
            throw invalid_argument("Capacity must be greater than zero.");
        }
    }

    // Insert or update page
    void put(const Key& key, const Value& value) {

        // Page already exists
        auto it = index_map_.find(key);

        if (it != index_map_.end()) {
            size_t frame_index = it->second;

            frames_[frame_index].value = value;
            frames_[frame_index].referenced = true;

            cout << "Updated existing page: "
                 << key
                 << " [Second chance granted]"
                 << endl;

            return;
        }

        // Search for free frame
        for (size_t i = 0; i < capacity_; i++) {

            if (!frames_[i].valid) {

                frames_[i].key = key;
                frames_[i].value = value;
                frames_[i].valid = true;
                frames_[i].referenced = true;

                index_map_[key] = i;

                cout << "Inserted page "
                     << key
                     << " into free frame "
                     << i
                     << endl;

                return;
            }
        }

        // Buffer full
        cout << "Buffer full. Searching victim for page "
             << key
             << endl;

        evictAndReplace(key, value);
    }

    // Access page
    optional<Value> get(const Key& key) {

        auto it = index_map_.find(key);

        if (it == index_map_.end()) {

            cout << "Page miss: " << key << endl;
            return nullopt;
        }

        size_t frame_index = it->second;

        // Give second chance
        frames_[frame_index].referenced = true;

        cout << "Page hit: "
             << key
             << " found in frame "
             << frame_index
             << endl;

        return frames_[frame_index].value;
    }

    bool contains(const Key& key) const {
        return index_map_.find(key) != index_map_.end();
    }

    void display() const {

        cout << "\nCurrent Buffer State\n";
        cout << "-------------------------------------------\n";
        cout << "Frame | Key | Value | Valid | Referenced\n";
        cout << "-------------------------------------------\n";

        for (size_t i = 0; i < capacity_; i++) {

            const auto& frame = frames_[i];

            cout << i << "     | ";

            if (frame.valid) {

                cout << frame.key << "   | "
                     << frame.value << "   | "
                     << "true"
                     << "   | "
                     << (frame.referenced ? "true" : "false")
                     << endl;

            } else {

                cout << "-   | -   | false | false\n";
            }
        }

        cout << endl;
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

            // If referenced -> second chance
            if (candidate.referenced) {

                cout << "Frame "
                     << clock_hand_
                     << " has reference bit = true. "
                     << "Giving second chance.\n";

                candidate.referenced = false;

                advanceClock();
            }

            // Evict page
            else {

                cout << "Evicting page "
                     << candidate.key
                     << " from frame "
                     << clock_hand_
                     << endl;

                index_map_.erase(candidate.key);

                insertAtClockHand(key, value);

                return;
            }
        }
    }

    void insertAtClockHand(const Key& key,
                           const Value& value) {

        frames_[clock_hand_].key = key;
        frames_[clock_hand_].value = value;
        frames_[clock_hand_].valid = true;
        frames_[clock_hand_].referenced = true;

        index_map_[key] = clock_hand_;

        cout << "Inserted page "
             << key
             << " into frame "
             << clock_hand_
             << endl;

        advanceClock();
    }

    void advanceClock() {
        clock_hand_ = (clock_hand_ + 1) % capacity_;
    }

    size_t capacity_;

    vector<Frame> frames_;

    unordered_map<Key, size_t> index_map_;

    size_t clock_hand_;
};

int main() {

    cout << "Clock Sweep Page Replacement Demo\n";

    ClockSweep<int, string> cache(3);

    cache.put(1, "PageA");
    cache.put(2, "PageB");
    cache.put(3, "PageC");

    cache.display();

    cache.get(1);
    cache.get(2);

    cache.display();

    cache.put(4, "PageD");

    cache.display();

    cache.get(1);
    cache.get(3);
    cache.get(2);

    cache.put(5, "PageE");

    cache.display();

    cout << "Demo Complete\n";

    return 0;
}