#include <iostream>
#include <vector>
#include <unordered_map>
#include <thread>
#include <mutex>

template<typename T>
class ClockSweep {
public:
    ClockSweep(int maxNumber): maxCacheSize(maxNumber), clockHand(0) {};

    T getKey(T key){
        std::lock_guard<std::mutex> lock(mtx);
        if (cacheMap.find(key) != cacheMap.end()) {
            int frameIndex = cacheMap[key];
            referenceBits[frameIndex] = true;
            return key;
        }
        return T{}; // Default value if not found
    }

    void putKey(T key){
        std::lock_guard<std::mutex> lock(mtx);
        // If key already exists, just set reference bit to true
        if (cacheMap.find(key) != cacheMap.end()) {
            int frameIndex = cacheMap[key];
            referenceBits[frameIndex] = true;
            return;
        }

        // If cache has space, append it
        if (frames.size() < maxCacheSize) {
            frames.push_back(key);
            referenceBits.push_back(true);
            cacheMap[key] = frames.size() - 1;
        } else {
            // Cache is full, use clock sweep to find a victim
            while (true) {
                if (!referenceBits[clockHand]) {
                    // Found a page with reference bit 0, evict it
                    cacheMap.erase(frames[clockHand]);
                    frames[clockHand] = key;
                    referenceBits[clockHand] = true;
                    cacheMap[key] = clockHand;
                    clockHand = (clockHand + 1) % maxCacheSize;
                    break;
                } else {
                    // Give second chance, reset reference bit and move hand
                    referenceBits[clockHand] = false;
                    clockHand = (clockHand + 1) % maxCacheSize;
                }
            }
        }
    }

private:
    uint maxCacheSize{0u};
    std::thread bgClockThread; // Optional: for background sweeping if needed
    
    std::vector<T> frames;
    std::vector<bool> referenceBits;
    std::unordered_map<T, int> cacheMap;
    int clockHand{0};
    std::mutex mtx;
};

int main(){
    ClockSweep<int> clockSweep(3); // Cache size of 3
    
    clockSweep.putKey(1);
    clockSweep.putKey(2);
    clockSweep.putKey(3);
    
    // 1, 2, 3 are in cache. Reference bits: 1, 1, 1
    clockSweep.getKey(1); // Set 1's ref bit to 1 again
    
    // Cache is full. 4 needs a victim.
    // Clock sweeps: clears ref bits for 1, 2, 3, then evicts 1
    clockSweep.putKey(4); 
    
    std::cout << "Got 1 (evicted): " << clockSweep.getKey(1) << std::endl; // Should be 0 (default/not found)
    std::cout << "Got 2: " << clockSweep.getKey(2) << std::endl; // Should be 2
    std::cout << "Got 4: " << clockSweep.getKey(4) << std::endl; // Should be 4
    
    return 0;
}
