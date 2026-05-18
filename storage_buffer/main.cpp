#include <iostream>
#include <vector>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <algorithm> // for std::min

template<typename T>
class ClockSweep {
public:
    ClockSweep(int maxNumber): maxCacheSize(maxNumber), clockHand(0) {};

    T getKey(T key){
        std::lock_guard<std::mutex> lock(mtx);
        if (cacheMap.find(key) != cacheMap.end()) {
            int frameIndex = cacheMap[key];
            // Increment usage count, up to a maximum (e.g., 3)
            useCounts[frameIndex] = std::min(useCounts[frameIndex] + 1, MAX_COUNT);
            return key;
        }
        return T{}; // Default value if not found
    }

    void putKey(T key){
        std::lock_guard<std::mutex> lock(mtx);
        // If key already exists, just increase its usage count
        if (cacheMap.find(key) != cacheMap.end()) {
            int frameIndex = cacheMap[key];
            useCounts[frameIndex] = std::min(useCounts[frameIndex] + 1, MAX_COUNT);
            return;
        }

        // If cache has space, append it
        if (frames.size() < maxCacheSize) {
            frames.push_back(key);
            useCounts.push_back(1); // Start with a count of 1
            cacheMap[key] = frames.size() - 1;
        } else {
            // Cache is full, use clock sweep to find a victim
            while (true) {
                if (useCounts[clockHand] == 0) {
                    // Found a page with count 0, evict it
                    cacheMap.erase(frames[clockHand]);
                    frames[clockHand] = key;
                    useCounts[clockHand] = 1; // Set initial count for new key
                    cacheMap[key] = clockHand;
                    clockHand = (clockHand + 1) % maxCacheSize;
                    break;
                } else {
                    // Decrement usage count (aging) and move hand
                    useCounts[clockHand]--;
                    clockHand = (clockHand + 1) % maxCacheSize;
                }
            }
        }
    }

private:
    uint maxCacheSize{0u};
    const int MAX_COUNT{3}; // Maximum usage score a key can accumulate
    std::thread bgClockThread; // Optional: for background sweeping if needed
    
    std::vector<T> frames;
    std::vector<int> useCounts; // Replaced referenceBits with usage counts
    std::unordered_map<T, int> cacheMap;
    int clockHand{0};
    std::mutex mtx;
};

int main(){
    ClockSweep<int> clockSweep(3); // Cache size of 3
    
    clockSweep.putKey(1);
    clockSweep.putKey(2);
    clockSweep.putKey(3);
    
    // 1, 2, 3 are in cache. Initial counts: 1, 1, 1
    
    // We access 1 multiple times to increase its count
    clockSweep.getKey(1); // 1's count becomes 2
    clockSweep.getKey(1); // 1's count becomes 3 (max)
    
    // We access 2 once
    clockSweep.getKey(2); // 2's count becomes 2
    
    // 3 is never accessed again (count stays 1)
    
    // Cache is full. 4 needs a victim.
    // The sweep will cycle through, decreasing counts. 
    // Key 3 will reach count 0 first because its count was the lowest.
    clockSweep.putKey(4); 
    
    std::cout << "Got 1: " << clockSweep.getKey(1) << std::endl; // Should be 1
    std::cout << "Got 2: " << clockSweep.getKey(2) << std::endl; // Should be 2
    std::cout << "Got 3 (evicted): " << clockSweep.getKey(3) << std::endl; // Should be 0
    std::cout << "Got 4: " << clockSweep.getKey(4) << std::endl; // Should be 4
    
    return 0;
}
