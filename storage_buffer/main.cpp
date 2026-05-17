#include <iostream>
#include <vector>
#include <unordered_map>

using uint = unsigned int;

template<typename T>
class ClockSweep {
public:
    ClockSweep(int maxNumber): maxCacheSize(maxNumber), hand(0u) {
        frames.resize(maxCacheSize);
    }

    // returns the key if it is in the cache, otherwise prints a miss
    T getKey(T key) {
        auto it = keyToSlot.find(key);
        if (it == keyToSlot.end()) {
            std::cout << "miss: " << key << "\n";
            return T();
        }
        uint slot = it->second;
        frames[slot].refBit = true;
        std::cout << "hit: " << key << "\n";
        return frames[slot].key;
    }

    void putKey(T key) {
        // already in cache -> just mark referenced
        auto it = keyToSlot.find(key);
        if (it != keyToSlot.end()) {
            frames[it->second].refBit = true;
            return;
        }

        // empty slot available? fill it directly
        for (uint i = 0; i < maxCacheSize; i++) {
            if (!frames[i].valid) {
                frames[i].key = key;
                frames[i].valid = true;
                frames[i].refBit = true;
                keyToSlot[key] = i;
                std::cout << "inserted " << key << " at slot " << i << "\n";
                return;
            }
        }

        // cache is full -> sweep until we find a frame with refBit = 0
        while (true) {
            if (frames[hand].refBit) {
                // second chance: clear the bit and move on
                frames[hand].refBit = false;
                hand = (hand + 1) % maxCacheSize;
                continue;
            }

            // refBit is 0 -> evict this one
            T victim = frames[hand].key;
            keyToSlot.erase(victim);

            frames[hand].key = key;
            frames[hand].refBit = true;
            keyToSlot[key] = hand;

            std::cout << "evicted " << victim << ", inserted " << key
                      << " at slot " << hand << "\n";

            hand = (hand + 1) % maxCacheSize;
            return;
        }
    }

    void printState() {
        std::cout << "------\n";
        for (uint i = 0; i < maxCacheSize; i++) {
            std::cout << "[" << i << "] ";
            if (frames[i].valid) {
                std::cout << "key=" << frames[i].key
                          << " ref=" << frames[i].refBit;
            } else {
                std::cout << "empty";
            }
            if (i == hand) std::cout << "  <- hand";
            std::cout << "\n";
        }
        std::cout << "------\n";
    }

private:
    struct Frame {
        T key{};
        bool refBit{false};
        bool valid{false};
    };

    uint maxCacheSize{0u};
    std::vector<Frame> frames;
    std::unordered_map<T, uint> keyToSlot;
    uint hand{0u};
};

int main() {
    ClockSweep<int> cache(4);

    // fill the cache
    cache.putKey(1);
    cache.putKey(2);
    cache.putKey(3);
    cache.putKey(4);
    cache.printState();

    // touch 1 and 3 (no-op for ref bits here since they're already 1)
    cache.getKey(1);
    cache.getKey(3);

    // cache is full, all ref bits are 1 -> sweep clears them all, evicts slot 0
    cache.putKey(5);
    cache.printState();

    // inserting 6 -> hand is at slot 1, slot 1's ref is 0, evicts 2
    cache.putKey(6);
    cache.printState();

    // quick hit/miss test
    cache.getKey(99);
    cache.getKey(5);

    return 0;
}
