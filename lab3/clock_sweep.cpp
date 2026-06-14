#include <iostream>
#include <unordered_map>
#include <vector>
#include <string>

template<typename T>
class ClockSweep {
public:
    explicit ClockSweep(int maxNumber)
        : maxCacheSize(maxNumber), clockHand(0) {}

    // Returns true on cache hit, false on miss.
    bool getKey(T key) {
        auto it = cacheMap.find(key);
        if (it != cacheMap.end()) {
            int idx = it->second;
            cache[idx].refBit = true;
            std::cout << "[HIT]    Key=" << key
                      << "  slot=" << idx << "  (ref bit set)\n";
            return true;
        }
        std::cout << "[MISS]   Key=" << key << "\n";
        return false;
    }

    void putKey(T key) {
        // Already cached – refresh ref bit.
        auto it = cacheMap.find(key);
        if (it != cacheMap.end()) {
            cache[it->second].refBit = true;
            std::cout << "[UPDATE] Key=" << key << "  ref bit refreshed\n";
            return;
        }

        if (static_cast<int>(cache.size()) < maxCacheSize) {
            int slot = static_cast<int>(cache.size());
            cache.push_back({key, true});
            cacheMap[key] = slot;
            std::cout << "[INSERT] Key=" << key << "  slot=" << slot << "\n";
            return;
        }

        // Cache full – run the clock hand to find an eviction victim.
        while (true) {
            CachePage& page = cache[clockHand];
            if (!page.refBit) {
                std::cout << "[EVICT]  Key=" << page.key
                          << "  slot=" << clockHand << "\n";
                cacheMap.erase(page.key);
                page = {key, true};
                cacheMap[key] = clockHand;
                std::cout << "[INSERT] Key=" << key
                          << "  slot=" << clockHand << "\n";
                clockHand = (clockHand + 1) % maxCacheSize;
                break;
            }
            // Give the page a second chance.
            std::cout << "[SWEEP]  Key=" << page.key
                      << "  slot=" << clockHand << "  ref bit cleared\n";
            page.refBit = false;
            clockHand = (clockHand + 1) % maxCacheSize;
        }
    }

    void printState() const {
        std::cout << "\n=== Cache State ===\n";
        for (int i = 0; i < static_cast<int>(cache.size()); i++) {
            std::cout << "  Slot " << i
                      << ": Key=" << cache[i].key
                      << "  RefBit=" << cache[i].refBit
                      << (i == clockHand ? "  <-- clock hand" : "") << "\n";
        }
        std::cout << "===================\n\n";
    }

private:
    struct CachePage {
        T    key;
        bool refBit;
    };

    int                          maxCacheSize;
    int                          clockHand;
    std::vector<CachePage>       cache;
    std::unordered_map<T, int>   cacheMap;
};

int main() {
    std::cout << "=== Lab 3: Clock Sweep Cache Replacement Algorithm ===\n\n";

    ClockSweep<int> cache(4);

    // Task 2: Populate cache
    std::cout << "--- Task 2: Cache Population ---\n";
    cache.putKey(1);
    cache.putKey(2);
    cache.putKey(3);
    cache.putKey(4);
    cache.printState();

    // Task 3: Access pattern – pages 1 and 3 accessed more frequently
    std::cout << "--- Task 3: Access Pattern Analysis ---\n";
    cache.getKey(1);
    cache.getKey(3);
    cache.getKey(1);
    cache.getKey(2);
    cache.printState();

    // Task 4 & 5: Clock sweep eviction when inserting new pages
    std::cout << "--- Task 4 & 5: Clock Sweep and Replacement ---\n";
    cache.putKey(5);  // page 4 (ref=0 after prior non-access) should be evicted
    cache.printState();

    cache.putKey(6);
    cache.printState();

    // Task 6: Miss after eviction
    std::cout << "--- Task 6: Cache Miss Verification ---\n";
    cache.getKey(4);  // was evicted
    cache.getKey(5);  // still present

    return 0;
}
