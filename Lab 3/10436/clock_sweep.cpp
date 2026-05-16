#include <iostream>
#include <vector>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <chrono>
#include <optional>

template <typename T>
class ClockSweep
{
public:
    ClockSweep(int maxNumber) : maxCacheSize(maxNumber)
    {
        frames.resize(maxCacheSize);
    }

    T getKey(const T &key)
    {
        std::lock_guard<std::mutex> lock(latch);

        auto it = pageTable.find(key);
        if (it == pageTable.end())
        {
            throw std::runtime_error("Page not found in buffer pool");
        }

        frames[it->second].referenced = true;
        hits++;
        return frames[it->second].key.value();
    }

    void putKey(const T &key)
    {
        std::lock_guard<std::mutex> lock(latch);

        if (pageTable.find(key) != pageTable.end())
        {
            frames[pageTable[key]].referenced = true;
            hits++;
            std::cout << "HIT  -> page " << key << "\n";
            return;
        }

        misses++;
        std::cout << "MISS -> page " << key;

        int target = findEmptyOrVictim();

        if (frames[target].key.has_value())
        {
            std::cout << " (evicted page " << frames[target].key.value()
                      << " from frame " << target << ")";
            pageTable.erase(frames[target].key.value());
            evictions++;
        }
        else
        {
            std::cout << " (loaded into empty frame " << target << ")";
        }

        frames[target].key = key;
        frames[target].referenced = true;
        pageTable[key] = target;

        std::cout << "\n";
    }

    void printBuffer()
    {
        std::lock_guard<std::mutex> lock(latch);

        std::cout << "\n  [Buffer State]\n";
        for (int i = 0; i < maxCacheSize; i++)
        {
            std::cout << "  frame " << i;

            if (frames[i].key.has_value())
            {
                std::cout << " | page=" << frames[i].key.value()
                          << " | bit=" << (frames[i].referenced ? "1" : "0");
            }
            else
            {
                std::cout << " | empty";
            }

            if (i == clockHand)
            {
                std::cout << "  <- clock hand";
            }

            std::cout << "\n";
        }
        std::cout << "\n";
    }

    void printStats()
    {
        std::lock_guard<std::mutex> lock(latch);

        int total = hits + misses;
        std::cout << "--- Stats ---\n";
        std::cout << "hits      : " << hits << "\n";
        std::cout << "misses    : " << misses << "\n";
        std::cout << "evictions : " << evictions << "\n";
        std::cout << "hit ratio : "
                  << (total == 0 ? 0.0 : (100.0 * hits / total))
                  << "%\n";
    }

    void startBackgroundSweeper()
    {
        bgClockThread = std::thread([this]()
                                    {
            while (true) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                std::lock_guard<std::mutex> lock(latch);
                std::cout << "\n[Background Sweeper] Scanning frames...\n";
                for (int i = 0; i < maxCacheSize; i++) {
                    if (frames[i].key.has_value() && frames[i].referenced) {
                        frames[i].referenced = false;
                        std::cout << "  cleared bit on frame " << i 
                                  << " (page " << frames[i].key.value() << ")\n";
                    }
                }
            } });
        bgClockThread.detach();
    }

private:
    struct Frame
    {
        std::optional<T> key;
        bool referenced = false;
    };

    int maxCacheSize;
    std::vector<Frame> frames;
    std::unordered_map<T, int> pageTable;
    int clockHand = 0;
    int hits = 0;
    int misses = 0;
    int evictions = 0;
    std::mutex latch;
    std::thread bgClockThread;

    int findEmptyOrVictim()
    {
        while (true)
        {
            Frame &frame = frames[clockHand];

            if (!frame.key.has_value())
            {
                int target = clockHand;
                clockHand = (clockHand + 1) % maxCacheSize;
                return target;
            }

            if (!frame.referenced)
            {
                int target = clockHand;
                clockHand = (clockHand + 1) % maxCacheSize;
                return target;
            }

            frame.referenced = false;
            clockHand = (clockHand + 1) % maxCacheSize;
        }
    }
};

int main()
{
    std::cout << "=== Clock Sweep Buffer Pool Demo ===\n\n";

    ClockSweep<int> cache(3);

    cache.startBackgroundSweeper();

    std::vector<int> pages = {1, 2, 3, 4, 1, 5, 2, 1};

    for (int page : pages)
    {
        cache.putKey(page);
    }

    cache.printBuffer();
    cache.printStats();

    std::this_thread::sleep_for(std::chrono::seconds(3));

    return 0;
}