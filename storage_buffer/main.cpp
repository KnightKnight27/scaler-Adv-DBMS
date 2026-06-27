#include <iostream>
#include <vector>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <chrono>

template<typename T>
class ClockSweep {
public:
    ClockSweep(int maxNumber) : maxCacheSize(maxNumber), hand(0), stopBgThread(false) {
        bgClockThread = std::thread(&ClockSweep::decayRefBitsLoop, this);
    }

    ~ClockSweep() {
        {
            std::lock_guard<std::mutex> lock(mtx);
            stopBgThread = true;
        }
        if (bgClockThread.joinable()) {
            bgClockThread.join();
        }
    }

    bool getKey(const T& key, T& result) {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = cacheMap.find(key);
        if (it != cacheMap.end()) {
            size_t idx = it->second;
            cache[idx].referenced = true;
            result = cache[idx].key;
            return true;
        }
        return false;
    }

    void putKey(const T& key) {
        std::lock_guard<std::mutex> lock(mtx);
        
        // If key already exists, update reference bit and return
        auto it = cacheMap.find(key);
        if (it != cacheMap.end()) {
            cache[it->second].referenced = true;
            return;
        }

        // Cache is not full, just append
        if (cache.size() < maxCacheSize) {
            cache.push_back({key, true});
            cacheMap[key] = cache.size() - 1;
            return;
        }

        // Cache is full, run Clock Sweep eviction
        while (true) {
            if (!cache[hand].referenced) {
                // Evict this page
                cacheMap.erase(cache[hand].key);
                cache[hand].key = key;
                cache[hand].referenced = true;
                cacheMap[key] = hand;
                hand = (hand + 1) % maxCacheSize;
                break;
            } else {
                // Clear reference bit and move hand
                cache[hand].referenced = false;
                hand = (hand + 1) % maxCacheSize;
            }
        }
    }

    void printCacheState() {
        std::lock_guard<std::mutex> lock(mtx);
        std::cout << "Cache contents: [ ";
        for (size_t i = 0; i < cache.size(); ++i) {
            std::cout << cache[i].key << "(ref=" << cache[i].referenced << ")";
            if (i == hand) std::cout << "*";
            std::cout << " ";
        }
        std::cout << "]\n";
    }

private:
    struct CacheEntry {
        T key;
        bool referenced;
    };

    void decayRefBitsLoop() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            std::lock_guard<std::mutex> lock(mtx);
            if (stopBgThread) break;
        }
    }

    size_t maxCacheSize;
    std::vector<CacheEntry> cache;
    std::unordered_map<T, size_t> cacheMap;
    size_t hand;
    std::thread bgClockThread;
    std::mutex mtx;
    bool stopBgThread;
};

int main() {
    std::cout << "Initializing ClockSweep cache with capacity 3...\n";
    ClockSweep<int> cache(3);

    std::cout << "Inserting keys 1, 2, 3...\n";
    cache.putKey(1);
    cache.putKey(2);
    cache.putKey(3);
    cache.printCacheState();

    std::cout << "Accessing key 1 (sets ref bit to true)...\n";
    int val;
    cache.getKey(1, val);
    cache.printCacheState();

    std::cout << "Inserting key 4 (causes eviction of 2, since 1 is referenced)...\n";
    cache.putKey(4);
    cache.printCacheState();

    std::cout << "Inserting key 5 (causes eviction of 3)...\n";
    cache.putKey(5);
    cache.printCacheState();

    return 0;
}
