#include <iostream>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <chrono>
#include <optional>

template<typename T>
class ClockSweep {
public:
    struct CacheRecord {
        T key;
        int accessFrequency;
    };

    ClockSweep(int maxNumber)
        : maxCacheSize(maxNumber), stopThread(false)
    {
        bgClockThread = std::thread(&ClockSweep::backgroundClock, this);
    }

    ~ClockSweep() {
        stopThread = true;
        if (bgClockThread.joinable()) {
            bgClockThread.join();
        }
    }

    std::optional<CacheRecord> getKey(const T& key) {
        std::lock_guard<std::mutex> lock(cacheMutex);

        auto it = cache.find(key);
        if (it != cache.end()) {
            it->second.accessFrequency++; 
            std::cout << "Key found: " << key
                      << ", Frequency = "
                      << it->second.accessFrequency << "\n";
            return it->second;
        }

        std::cout << "Key not found\n";
        return std::nullopt;
    }

    void putKey(const T& key) {
        std::lock_guard<std::mutex> lock(cacheMutex);

        if (cache.find(key) != cache.end()) {
            std::cout << "Key already exists\n";
            return;
        }

        if (cache.size() < maxCacheSize) {
            cache[key] = {key, 5};
            std::cout << "Inserted key: " << key << "\n";
            return;
        }

        auto victimIt = cache.begin();
        for (auto it = cache.begin(); it != cache.end(); ++it) {
            if (it->second.accessFrequency < victimIt->second.accessFrequency) {
                victimIt = it;
            }
        }

        std::cout << "Removing key: "
                  << victimIt->first
                  << " with frequency "
                  << victimIt->second.accessFrequency
                  << "\n";

        cache.erase(victimIt);

        cache[key] = {key, 5};
        std::cout << "Inserted key: " << key << "\n";
    }

    void printCache() {
        std::lock_guard<std::mutex> lock(cacheMutex);

        std::cout << "\nCurrent Cache:\n";
        for (auto& [key, record] : cache) {
            std::cout << "Key = " << key
                      << ", Frequency = " << record.accessFrequency
                      << "\n";
        }
        std::cout << "\n";
    }

private:
    void backgroundClock() {
        while (!stopThread) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::lock_guard<std::mutex> lock(cacheMutex);
            for (auto& [key, record] : cache) {
                if (record.accessFrequency > 0) {
                    record.accessFrequency--;
                }
            }
        }
    }

    size_t maxCacheSize;
    std::unordered_map<T, CacheRecord> cache;
    std::thread bgClockThread;
    std::mutex cacheMutex;
    bool stopThread;
};

int main() {
    ClockSweep<int> clockSweep(3);

    clockSweep.putKey(10);
    clockSweep.putKey(20);
    clockSweep.putKey(30);

    clockSweep.printCache();

    std::this_thread::sleep_for(std::chrono::seconds(3));

    clockSweep.getKey(10);

    clockSweep.printCache();

    clockSweep.putKey(40);

    clockSweep.printCache();

    return 0;
}
