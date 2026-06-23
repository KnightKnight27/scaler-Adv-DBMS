#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>

template<typename T>
class ClockSweep {
public:

    ClockSweep(int maxNumber)
        : maxCacheSize(maxNumber)
    {
        bgClockThread =
            std::thread(&ClockSweep::runClockSweep, this);
    }

    ~ClockSweep() {
        stopThread = true;

        if (bgClockThread.joinable())
            bgClockThread.join();
    }

    T getKey(T key) {

        std::lock_guard<std::mutex> lock(cacheMutex);

        for (auto &entry : cache) {

            if (entry.key == key) {

                entry.referenceBit = true;

                std::cout
                    << "Accessed key "
                    << key
                    << " -> reference bit set to 1"
                    << std::endl;

                return entry.key;
            }
        }

        std::cout
            << "Key "
            << key
            << " not found in cache"
            << std::endl;

        return T{};
    }

    void putKey(T key) {

        std::lock_guard<std::mutex> lock(cacheMutex);

        for (auto &entry : cache) {

            if (entry.key == key) {

                entry.referenceBit = true;

                std::cout
                    << "Key "
                    << key
                    << " already exists -> reference bit set to 1"
                    << std::endl;

                return;
            }
        }

        if (cache.size() < maxCacheSize) {

            cache.push_back({key, true});

            std::cout
                << "Inserted "
                << key
                << " into cache"
                << std::endl;

            return;
        }

        while (true) {

            if (clockHand >= cache.size())
                clockHand = 0;

            if (cache[clockHand].referenceBit) {

                std::cout
                    << "Second chance given to "
                    << cache[clockHand].key
                    << std::endl;

                cache[clockHand].referenceBit = false;
            }
            else {

                std::cout
                    << "Evicting "
                    << cache[clockHand].key
                    << " -> inserting "
                    << key
                    << std::endl;

                cache[clockHand].key = key;
                cache[clockHand].referenceBit = true;

                clockHand++;
                break;
            }

            clockHand++;
        }
    }

    void displayCache() {

        std::lock_guard<std::mutex> lock(cacheMutex);

        std::cout << "\nCurrent Cache State:\n";

        for (const auto &entry : cache) {

            std::cout
                << "[Key: "
                << entry.key
                << ", RefBit: "
                << entry.referenceBit
                << "] ";
        }

        std::cout << "\n" << std::endl;
    }

private:

    struct CacheEntry {

        T key;
        bool referenceBit;
    };

    void runClockSweep() {

        while (!stopThread) {

            std::this_thread::sleep_for(
                std::chrono::seconds(5)
            );
        }
    }

    uint maxCacheSize{0u};

    std::vector<CacheEntry> cache;

    int clockHand{0};

    std::thread bgClockThread;

    std::mutex cacheMutex;

    bool stopThread{false};
};

int main() {

    ClockSweep<int> clockSweep(3);

    std::cout << "=== Initial Insertions ===" << std::endl;

    clockSweep.putKey(1);
    clockSweep.putKey(2);
    clockSweep.putKey(3);

    clockSweep.displayCache();

    std::cout << "=== Access Key 1 ===" << std::endl;

    clockSweep.getKey(1);

    clockSweep.displayCache();

    std::cout << "=== Insert Key 4 (Triggers Clock Sweep) ===" << std::endl;

    clockSweep.putKey(4);

    clockSweep.displayCache();

    return 0;
}