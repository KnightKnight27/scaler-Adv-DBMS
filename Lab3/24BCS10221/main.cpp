#include <iostream>
#include <vector>
#include <unordered_map>
#include <optional>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>

template<typename T>
class ClockSweep {
public:
    ClockSweep(int maxNumber = 16384) : maxCacheSize(maxNumber) {
        buffer.resize(maxCacheSize);
        bgClockThread = std::thread(&ClockSweep::sweepLoop, this);
    }

    ~ClockSweep() {
        running = false;
        if (bgClockThread.joinable()) {
            bgClockThread.join();
        }
    }

    std::optional<T> get(T key) {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = index.find(key);
        if (it != index.end()) {
            size_t pos = it->second;
            buffer[pos].inUse = true;
            if (buffer[pos].usageCount < 5) {
                buffer[pos].usageCount++;
            }
            return buffer[pos].key;
        }
        return std::nullopt;
    }

    void put(T key) {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = index.find(key);
        if (it != index.end()) {
            buffer[it->second].inUse = true;
            if (buffer[it->second].usageCount < 5) {
                buffer[it->second].usageCount++;
            }
            return;
        }

        size_t pos = findFreeSlot();
        if (pos == maxCacheSize) {
            pos = evict();
            if (pos == maxCacheSize) {
                return; // cache is full and all pages are in use
            }
        }

        buffer[pos].key = key;
        buffer[pos].usageCount = 1;
        buffer[pos].inUse = true;
        buffer[pos].occupied = true;
        index[key] = pos;
    }

    void release(T key) {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = index.find(key);
        if (it != index.end()) {
            buffer[it->second].inUse = false;
        }
    }

    void printState() {
        std::lock_guard<std::mutex> lock(mtx);
        std::cout << "Buffer pool state\n";
        std::cout << "Clock Hand Position: " << clockHand << "\n";
        for (size_t i = 0; i < maxCacheSize; ++i) {
            if (buffer[i].occupied) {
                std::cout << "Slot " << i << ": key=" << buffer[i].key
                          << " usageCount=" << static_cast<int>(buffer[i].usageCount)
                          << " inUse=" << (buffer[i].inUse ? "true" : "false") << "\n";
            }
        }
    }

private:
    struct Frame {
        T key{};
        uint8_t usageCount = 0;
        bool inUse = false;
        bool occupied = false;
    };

    void sweepLoop() {
        while (running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::lock_guard<std::mutex> lock(mtx);
            for (size_t i = 0; i < maxCacheSize; ++i) {
                size_t pos = clockHand;
                clockHand = (clockHand + 1) % maxCacheSize;
                if (!buffer[pos].occupied) continue;
                if (buffer[pos].inUse) continue;
                if (buffer[pos].usageCount > 0) {
                    buffer[pos].usageCount--;
                }
            }
        }
    }

    size_t findFreeSlot() {
        for (size_t i = 0; i < maxCacheSize; ++i) {
            if (!buffer[i].occupied) {
                return i;
            }
        }
        return maxCacheSize;
    }

    size_t evict() {
        size_t maxPasses = 5;
        for (size_t pass = 0; pass < maxPasses; ++pass) {
            for (size_t checked = 0; checked < maxCacheSize; ++checked) {
                size_t pos = clockHand;
                clockHand = (clockHand + 1) % maxCacheSize;
                if (!buffer[pos].occupied || buffer[pos].inUse) {
                    continue;
                }
                if (buffer[pos].usageCount == 0) {
                    T evictedKey = buffer[pos].key;
                    buffer[pos].occupied = false;
                    buffer[pos].inUse = false;
                    buffer[pos].usageCount = 0;
                    index.erase(evictedKey);
                    return pos;
                }
                buffer[pos].usageCount--;
            }
        }
        return maxCacheSize;
    }

    unsigned int maxCacheSize;
    std::vector<Frame> buffer;
    std::unordered_map<T, size_t> index;
    size_t clockHand{0};
    std::mutex mtx;
    std::atomic<bool> running{true};
    std::thread bgClockThread;
};

int main() {
    std::cout << "Testing ClockSweep implementation..." << std::endl;
    ClockSweep<int> clockSweep(5);
    
    clockSweep.put(1);
    clockSweep.put(2);
    clockSweep.put(3);
    clockSweep.put(4);
    clockSweep.put(5);

    clockSweep.printState();

    clockSweep.release(1);
    clockSweep.release(2);
    
    std::cout << "Waiting 2 seconds for background thread to sweep..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    clockSweep.printState();

    std::cout << "Putting key 6, should evict a key with 0 usageCount" << std::endl;
    clockSweep.put(6); 
    clockSweep.printState();

    auto val = clockSweep.get(3);
    if (val) {
        std::cout << "Successfully got key " << *val << std::endl;
    }

    return 0;
}
