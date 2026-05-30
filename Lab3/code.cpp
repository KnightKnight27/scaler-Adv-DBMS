#include <iostream>
#include <vector>
#include <unordered_map>

template <typename T>
class ClockSweep {
public:
    ClockSweep(int maxNumber) : maxCacheSize(maxNumber) {
        slots.reserve(maxCacheSize);
    }

    // put a page into the buffer
    void access(T key) {
        // check if page is already in buffer
        if (keyToSlot.find(key) != keyToSlot.end()) {
            int idx = keyToSlot[key];
            slots[idx].usageBit = true;
            hits++;
            std::cout << "HIT  -> page " << key << "\n";
            return;
        }

        // page not found, its a miss
        misses++;
        std::cout << "MISS -> page " << key;

        // if buffer still has space just add it
        if ((int)slots.size() < maxCacheSize) {
            slots.push_back({key, true});
            keyToSlot[key] = slots.size() - 1;
            std::cout << " (loaded into empty frame " << slots.size() - 1 << ")\n";
            return;
        }

        // buffer is full, run clock sweep
        int victim = findVictim();
        std::cout << " (evicted page " << slots[victim].key << " from frame " << victim << ")\n";

        keyToSlot.erase(slots[victim].key);
        slots[victim].key = key;
        slots[victim].usageBit = true;
        keyToSlot[key] = victim;

        // move hand forward after eviction
        clockHand = (victim + 1) % maxCacheSize;
    }

    void printBuffer() {
        std::cout << "\n  [Buffer]\n";
        for (int i = 0; i < (int)slots.size(); i++) {
            std::cout << "  frame " << i
                      << " | page=" << slots[i].key
                      << " | bit=" << slots[i].usageBit
                      << (i == clockHand ? "  <- clock" : "")
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
        bool usageBit = false;
    };

    int maxCacheSize;
    std::vector<Slot> slots;
    std::unordered_map<T, int> keyToSlot;
    int clockHand = 0;
    int hits      = 0;
    int misses    = 0;
    int evictions = 0;

    int findVictim() {
        while (true) {
            if (slots[clockHand].usageBit) {
                // give it a second chance
                slots[clockHand].usageBit = false;
                clockHand = (clockHand + 1) % maxCacheSize;
            } else {
                // usage bit is 0, evict this one
                evictions++;
                return clockHand;
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