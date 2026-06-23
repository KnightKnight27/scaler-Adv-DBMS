#include <iostream>
#include <unordered_map>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <atomic>

template<typename T>
class ClockSweep {
public:
    explicit ClockSweep(size_t maxNumber)
        : maxCacheSize(maxNumber),
          clockHand(0),
          stopThread(false)
    {
        bgClockThread = std::thread(&ClockSweep::clockWorker, this);
    }

    ~ClockSweep() {
        stopThread = true;

        if (bgClockThread.joinable()) {
            bgClockThread.join();
        }
    }

    // Returns the key if found
    // Throws exception if not found
    T getKey(const T& key) {
        std::lock_guard<std::mutex> lock(cacheMutex);

        auto it = keyIndex.find(key);

        if (it == keyIndex.end()) {
            throw std::runtime_error("Key not found");
        }

        // Mark as recently used
        referenceBits[it->second] = true;

        return cache[it->second];
    }

    void putKey(const T& key) {
        std::lock_guard<std::mutex> lock(cacheMutex);

        // Key already exists
        auto existing = keyIndex.find(key);

        if (existing != keyIndex.end()) {
            referenceBits[existing->second] = true;
            return;
        }

        // Cache not full
        if (cache.size() < maxCacheSize) {
            cache.push_back(key);
            referenceBits.push_back(true);
            keyIndex[key] = cache.size() - 1;

            return;
        }

        // Clock replacement
        while (true) {
            if (!referenceBits[clockHand]) {
                // Remove old mapping
                keyIndex.erase(cache[clockHand]);

                // Replace with new key
                cache[clockHand] = key;
                referenceBits[clockHand] = true;
                keyIndex[key] = clockHand;

                clockHand = (clockHand + 1) % maxCacheSize;
                break;
            }

            // Give second chance
            referenceBits[clockHand] = false;
            clockHand = (clockHand + 1) % maxCacheSize;
        }
    }

    void printCache() {
        std::lock_guard<std::mutex> lock(cacheMutex);

        std::cout << "Cache: ";

        for (size_t i = 0; i < cache.size(); ++i) {
            std::cout << cache[i]
                      << "(" << referenceBits[i] << ") ";
        }

        std::cout << "\n";
    }

private:
    void clockWorker() {
        while (!stopThread) {
            std::this_thread::sleep_for(std::chrono::seconds(5));

            std::lock_guard<std::mutex> lock(cacheMutex);

            std::cout << "[Background Clock Sweep Running]\n";
        }
    }

private:
    size_t maxCacheSize{0};

    std::vector<T> cache;
    std::vector<bool> referenceBits;

    std::unordered_map<T, size_t> keyIndex;

    size_t clockHand{0};

    std::thread bgClockThread;
    std::mutex cacheMutex;

    std::atomic<bool> stopThread;
};

int main() {
    ClockSweep<int> clockSweep(4);

    clockSweep.putKey(1);
    clockSweep.putKey(2);
    clockSweep.putKey(3);
    clockSweep.putKey(4);

    clockSweep.printCache();

    // Access some keys
    clockSweep.getKey(2);
    clockSweep.getKey(3);

    // Add new key -> triggers replacement
    clockSweep.putKey(5);

    clockSweep.printCache();

    // Add another key
    clockSweep.putKey(6);

    clockSweep.printCache();

    return 0;
}