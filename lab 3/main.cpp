#include <iostream>
#include <vector>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <chrono>
#include <condition_variable>

template<typename T>
class ClockSweep {
public:
    ClockSweep(int maxNumber) : maxCacheSize(maxNumber), stopThread(false) {
        // Start a background thread as requested by the skeleton.
        bgClockThread = std::thread(&ClockSweep::backgroundTask, this);
    }

    ~ClockSweep() {
        {
            std::lock_guard<std::mutex> lock(mtx);
            stopThread = true;
        }
        cv.notify_one();
        if (bgClockThread.joinable()) {
            bgClockThread.join();
        }
    }

    T getKey(T key) {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = cacheMap.find(key);
        if (it != cacheMap.end()) {
            // Found the key, set the reference bit to 1
            clockBuffer[it->second].refBit = true;
            return key;
        }
        // Cache miss, return default constructed object
        return T(); 
    }

    void putKey(T key) {
        std::lock_guard<std::mutex> lock(mtx);

        // If the key is already in the cache, simply set its reference bit
        auto it = cacheMap.find(key);
        if (it != cacheMap.end()) {
            clockBuffer[it->second].refBit = true;
            return;
        }

        // If there's still room in the cache, just append it
        if (clockBuffer.size() < maxCacheSize) {
            clockBuffer.push_back({key, true});
            cacheMap[key] = clockBuffer.size() - 1;
            return;
        }

        // Cache is full. Perform the clock sweep algorithm to find a victim.
        while (true) {
            if (!clockBuffer[clockHand].refBit) {
                // We found a victim (reference bit is false)
                T victimKey = clockBuffer[clockHand].key;
                cacheMap.erase(victimKey);

                // Replace the victim with the new key and set reference bit to true
                clockBuffer[clockHand] = {key, true};
                cacheMap[key] = clockHand;

                // Move the hand forward
                clockHand = (clockHand + 1) % maxCacheSize;
                break;
            } else {
                // Give the item a second chance, reset reference bit to false, and move the hand
                clockBuffer[clockHand].refBit = false;
                clockHand = (clockHand + 1) % maxCacheSize;
            }
        }
    }

private:
    struct CacheEntry {
        T key;
        bool refBit;
    };

    uint maxCacheSize{0u};
    std::thread bgClockThread;
    
    // Concurrency and lifecycle management
    std::mutex mtx;
    std::condition_variable cv;
    bool stopThread;

    // Cache data structures
    std::vector<CacheEntry> clockBuffer;
    std::unordered_map<T, size_t> cacheMap;
    size_t clockHand{0};

    // Background thread function
    void backgroundTask() {
        std::unique_lock<std::mutex> lock(mtx);
        while (!stopThread) {
            // Wait for 1 second or until stopped
            cv.wait_for(lock, std::chrono::seconds(1));
            if (stopThread) break;
            
            // Background process logic goes here if needed.
        }
    }
};

int main() {
    // Initializing the cache with a size of 3
    ClockSweep<int> clockSweep(3);

    clockSweep.putKey(10);
    clockSweep.putKey(20);
    clockSweep.putKey(30);

    std::cout << "Added 10, 20, 30 to cache.\n";

    // This sets the reference bit for 10 to true
    std::cout << "Accessed 10: " << clockSweep.getKey(10) << "\n"; 

    // Adding 40 should evict 20, since 10 was recently accessed (given second chance)
    // and 20 is the next oldest item with a false reference bit after 10's reference bit gets cleared.
    std::cout << "Adding 40...\n";
    clockSweep.putKey(40);

    std::cout << "Is 20 in cache? (0 means no): " << clockSweep.getKey(20) << "\n";
    std::cout << "Is 10 in cache? " << clockSweep.getKey(10) << "\n";
    std::cout << "Is 40 in cache? " << clockSweep.getKey(40) << "\n";

    return 0;
}
