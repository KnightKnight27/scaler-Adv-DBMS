
#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <atomic>
#include <iomanip>

using uint = unsigned int;

template<typename T>
class ClockSweep {
public:
    struct CacheEntry {
        T key;
        bool referenced{false};
        bool valid{false};
    };

    ClockSweep(int maxNumber = 4) : maxCacheSize(static_cast<uint>(maxNumber)), buffer(maxNumber), clockHand(0u) {
        bgClockThread = std::thread(&ClockSweep::bgSweepWork, this);
    }

    ~ClockSweep() {
        stopBgThread = true;
        if (bgClockThread.joinable()) {
            bgClockThread.join();
        }
    }

    T getKey(T key) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        
        // Search if key already exists in cache (Cache Hit)
        for (uint i = 0u; i < maxCacheSize; ++i) {
            if (buffer[i].valid && buffer[i].key == key) {
                buffer[i].referenced = true;
                std::cout << "[GET] Hit: " << key << " (marked referenced)" << std::endl;
                printCacheState();
                return buffer[i].key;
            }
        }

        // Cache miss: insert key into the buffer
        std::cout << "[GET] Miss: " << key << " -> Loading into cache" << std::endl;
        putKeyInternal(key);
        return key;
    }

    void putKey(T key) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        putKeyInternal(key);
    }

    void printCacheState() const {
        std::cout << "--- Cache State (Hand points to slot " << clockHand << ") ---" << std::endl;
        for (uint i = 0u; i < maxCacheSize; ++i) {
            std::cout << "Slot [" << i << "]: ";
            if (buffer[i].valid) {
                std::cout << "Key = " << std::setw(3) << buffer[i].key 
                          << " | Ref = " << (buffer[i].referenced ? "1" : "0");
            } else {
                std::cout << "Empty";
            }
            if (i == clockHand) {
                std::cout << " <-- (Hand)";
            }
            std::cout << std::endl;
        }
        std::cout << "-----------------------------------------------" << std::endl;
    }

private:
    uint maxCacheSize{0u};
    std::vector<CacheEntry> buffer;
    uint clockHand{0u};
    std::mutex cacheMutex;
    std::thread bgClockThread;
    std::atomic<bool> stopBgThread{false};

    void putKeyInternal(T key) {
        // 1. If key is already in the cache, just mark it referenced and return
        for (uint i = 0u; i < maxCacheSize; ++i) {
            if (buffer[i].valid && buffer[i].key == key) {
                buffer[i].referenced = true;
                std::cout << "[PUT] Existing: " << key << " (marked referenced)" << std::endl;
                printCacheState();
                return;
            }
        }

        // 2. If there is an empty slot, use it
        for (uint i = 0u; i < maxCacheSize; ++i) {
            if (!buffer[i].valid) {
                buffer[i].key = key;
                buffer[i].valid = true;
                buffer[i].referenced = true;
                std::cout << "[PUT] Inserted empty: " << key << " at slot " << i << std::endl;
                printCacheState();
                return;
            }
        }

        // 3. Cache is full: perform Clock Sweep eviction
        std::cout << "[PUT] Cache Full. Performing Clock Sweep replacement to insert " << key << "..." << std::endl;
        while (true) {
            if (!buffer[clockHand].valid) {
                buffer[clockHand].key = key;
                buffer[clockHand].valid = true;
                buffer[clockHand].referenced = true;
                std::cout << "[PUT] Inserted empty (sweep): " << key << " at slot " << clockHand << std::endl;
                clockHand = (clockHand + 1) % maxCacheSize;
                printCacheState();
                return;
            }

            if (buffer[clockHand].referenced) {
                // Give second chance: clear referenced bit, advance hand
                buffer[clockHand].referenced = false;
                std::cout << "Sweep: Slot " << clockHand << " (Key " << buffer[clockHand].key 
                          << ") has Ref=1 -> Cleared to Ref=0" << std::endl;
                clockHand = (clockHand + 1) % maxCacheSize;
            } else {
                // Found eviction victim (referenced bit is 0)
                std::cout << "Evicting Key " << buffer[clockHand].key << " from slot " << clockHand 
                          << " to insert Key " << key << std::endl;
                
                buffer[clockHand].key = key;
                buffer[clockHand].referenced = true;
                clockHand = (clockHand + 1) % maxCacheSize;
                printCacheState();
                return;
            }
        }
    }

    void bgSweepWork() {
        while (!stopBgThread) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
            if (stopBgThread) break;
            
            std::lock_guard<std::mutex> lock(cacheMutex);
            bool decayed = false;
            for (uint i = 0u; i < maxCacheSize; ++i) {
                if (buffer[i].valid && buffer[i].referenced) {
                    buffer[i].referenced = false;
                    decayed = true;
                }
            }
            if (decayed) {
                std::cout << "\n[BG Sweep] Aged cache entries (decayed reference bits to 0)" << std::endl;
                printCacheState();
            }
        }
    }
};

int main() {
    std::cout << "=== CLOCK SWEEP BUFFER CACHE DEMO ===" << std::endl;
    
    // Create ClockSweep cache of capacity 4
    ClockSweep<int> clockSweep(4);
    
    // 1. Fill the cache
    clockSweep.putKey(10);
    clockSweep.putKey(20);
    clockSweep.putKey(30);
    clockSweep.putKey(40);
    
    // 2. Hit some keys to set their reference bits to 1
    std::cout << "\n--- Accessing keys to set reference bits ---" << std::endl;
    clockSweep.getKey(10); // Hit: slot 0, Ref=1
    clockSweep.getKey(30); // Hit: slot 2, Ref=1
    
    // 3. Insert a new key to trigger clock sweep eviction
    std::cout << "\n--- Inserting new key 50 (should evict 20 at slot 1, since slot 0 (10) has Ref=1) ---" << std::endl;
    clockSweep.putKey(50);
    
    // 4. Insert another key 60 (should evict 40 at slot 3, since slot 2 (30) has Ref=1)
    std::cout << "\n--- Inserting new key 60 (should evict 40 at slot 3) ---" << std::endl;
    clockSweep.putKey(60);

    // 5. Let's do some concurrent access to demonstrate thread safety
    std::cout << "\n--- Spawning concurrent threads to test thread safety ---" << std::endl;
    std::thread t1([&]() {
        for (int i = 0; i < 3; ++i) {
            clockSweep.getKey(10);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });

    std::thread t2([&]() {
        for (int i = 0; i < 3; ++i) {
            clockSweep.putKey(100 + i);
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        }
    });

    t1.join();
    t2.join();

    std::cout << "\n--- Allowing background sweep thread to run ---" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));

    std::cout << "\n=== DEMO END ===" << std::endl;
    return 0;
}

