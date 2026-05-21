#include <iostream>
#include <vector>
#include <unordered_map>

template <typename T>
class ClockSweep {
public:
    ClockSweep(int maxNumber) : capacity(maxNumber) {
        frames.reserve(capacity);
    }

    // put a page into the buffer
    void access(T key) {
        auto found = pageToFrame.find(key);
        if (found != pageToFrame.end()) {
            frames[found->second].referenced = true;
            hits++;
            std::cout << "HIT  -> page " << key << "\n";
            return;
        }

        misses++;
        std::cout << "MISS -> page " << key;

        if ((int)frames.size() < capacity) {
            frames.push_back({key, true});
            pageToFrame[key] = static_cast<int>(frames.size() - 1);
            std::cout << " (loaded into empty frame " << frames.size() - 1 << ")\n";
            return;
        }

        int victimIndex = chooseVictimFrame();
        std::cout << " (evicted page " << frames[victimIndex].key << " from frame " << victimIndex << ")\n";

        pageToFrame.erase(frames[victimIndex].key);
        frames[victimIndex].key = key;
        frames[victimIndex].referenced = true;
        pageToFrame[key] = victimIndex;

        hand = (victimIndex + 1) % capacity;
    }

    void printBuffer() {
        std::cout << "\n  [Buffer]\n";
        for (int i = 0; i < (int)frames.size(); i++) {
            std::cout << "  frame " << i
                      << " | page=" << frames[i].key
                      << " | bit=" << frames[i].referenced
                      << (i == hand ? "  <- clock" : "")
                      << "\n";
        }
        std::cout << "\n";
    }

    void printStats() {
        int total = hits + misses;
        std::cout << "\n--- Stats ---\n";
        std::cout << "hits      : " << hits << "\n";
        std::cout << "misses    : " << misses << "\n";
        std::cout << "evictions : " << evictions << "\n";
        std::cout << "hit ratio : " << (total == 0 ? 0.0 : (100.0 * hits / total)) << "%\n";
    }

private:
    struct Slot {
        T    key;
        bool referenced = false;
    };

    int capacity;
    std::vector<Slot> frames;
    std::unordered_map<T, int> pageToFrame;
    int hand = 0;
    int hits = 0;
    int misses = 0;
    int evictions = 0;

    int chooseVictimFrame() {
        while (true) {
            if (frames[hand].referenced) {
                frames[hand].referenced = false;
                hand = (hand + 1) % capacity;
            } else {
                evictions++;
                return hand;
            }
        }
    }
};

int main() {
    std::cout << "=== Clock Sweep Demo ===\n\n";

    ClockSweep<int> cache(4);

    // simulating page accesses
    std::vector<int> pages = {1, 2, 3, 4, 1, 5, 2, 1};

    for (int page : pages) {
        cache.access(page);
    }

    cache.printBuffer();
    cache.printStats();

    return 0;
}