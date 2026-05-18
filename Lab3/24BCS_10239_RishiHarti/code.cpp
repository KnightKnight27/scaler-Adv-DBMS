#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <unordered_map>

template <typename T>
class ClockSweepCache {
private:
    struct PageFrame {
        T data;
        bool referenceBit;
        bool occupied;

        PageFrame() : referenceBit(false), occupied(false) {}
    };

    std::vector<PageFrame> frames;
    unsigned int capacity;
    unsigned int clockPointer;
    std::mutex cacheMutex;
    std::thread clockThread;
    bool terminateClock;

    void clockSweepWorker() {
        while (!terminateClock) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            std::lock_guard<std::mutex> lock(cacheMutex);

            for (unsigned int i = 0; i < capacity; ++i) {
                unsigned int idx = (clockPointer + i) % capacity;
                if (frames[idx].occupied && frames[idx].referenceBit) {
                    frames[idx].referenceBit = false;
                }
            }
            clockPointer = (clockPointer + 1) % capacity;
        }
    }

    int findVictim() {
        unsigned int start = clockPointer;
        while (true) {
            if (!frames[clockPointer].occupied) {
                return clockPointer;
            }
            if (!frames[clockPointer].referenceBit) {
                return clockPointer;
            }
            frames[clockPointer].referenceBit = false;
            clockPointer = (clockPointer + 1) % capacity;

            if (clockPointer == start) {
                return clockPointer;
            }
        }
    }

public:
    ClockSweepCache(unsigned int size) : capacity(size), clockPointer(0), terminateClock(false) {
        frames.resize(capacity);
        clockThread = std::thread(&ClockSweepCache::clockSweepWorker, this);
    }

    ~ClockSweepCache() {
        terminateClock = true;
        if (clockThread.joinable()) {
            clockThread.join();
        }
    }

    bool lookup(const T& key) {
        std::lock_guard<std::mutex> lock(cacheMutex);

        for (auto& frame : frames) {
            if (frame.occupied && frame.data == key) {
                frame.referenceBit = true;
                return true;
            }
        }
        return false;
    }

    void insert(const T& key) {
        std::lock_guard<std::mutex> lock(cacheMutex);

        for (auto& frame : frames) {
            if (frame.occupied && frame.data == key) {
                frame.referenceBit = true;
                return;
            }
        }

        int victimIdx = findVictim();
        frames[victimIdx].data = key;
        frames[victimIdx].referenceBit = true;
        frames[victimIdx].occupied = true;
        clockPointer = (victimIdx + 1) % capacity;
    }

    void display() {
        std::lock_guard<std::mutex> lock(cacheMutex);
        std::cout << "\n=== Clock Sweep Cache State ===\n";
        for (unsigned int i = 0; i < capacity; ++i) {
            std::cout << "Frame " << i << ": ";
            if (frames[i].occupied) {
                std::cout << frames[i].data
                          << " [R=" << (frames[i].referenceBit ? "1" : "0") << "]";
            } else {
                std::cout << "EMPTY";
            }
            if (i == clockPointer) {
                std::cout << " <-- HAND";
            }
            std::cout << "\n";
        }
    }
};

int main() {
    ClockSweepCache<int> cache(4);

    std::cout << "Inserting pages: 10, 20, 30, 40\n";
    cache.insert(10);
    cache.insert(20);
    cache.insert(30);
    cache.insert(40);
    cache.display();

    std::cout << "\nAccessing page 20 (setting reference bit)\n";
    cache.lookup(20);
    cache.display();

    std::cout << "\nInserting new page 50 (eviction expected)\n";
    cache.insert(50);
    cache.display();

    std::cout << "\nAccessing page 10\n";
    cache.lookup(10);
    cache.display();

    return 0;
}