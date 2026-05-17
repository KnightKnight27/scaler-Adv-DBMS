// Lab 3 - Clock Sweep buffer replacement
// Vanditabyaa Dwivedi (24BCS10505)

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

template <typename T>
class ClockSweep {
public:
    explicit ClockSweep(std::size_t maxNumber)
        : maxCacheSize(maxNumber), frames(maxNumber), clockHand(0), stopFlag(false) {
        backgroundClockThread = std::thread(&ClockSweep::sweepLoop, this);
    }

    ~ClockSweep() {
        {
            std::lock_guard<std::mutex> lock(cacheMutex);
            stopFlag = true;
        }
        conditionVariable.notify_all();
        if (backgroundClockThread.joinable()) backgroundClockThread.join();
    }

    ClockSweep(const ClockSweep&) = delete;
    ClockSweep& operator=(const ClockSweep&) = delete;

    bool getKey(const T& key) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto iterator = index.find(key);
        if (iterator == index.end()) return false;
        Frame& frame = frames[iterator->second];
        if (frame.usage < kMaxUsage) frame.usage++;
        return true;
    }

    void putKey(const T& key) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        if (index.count(key)) {
            Frame& frame = frames[index[key]];
            if (frame.usage < kMaxUsage) frame.usage++;
            return;
        }
        std::size_t slot = pickVictim();
        Frame& frame = frames[slot];
        if (frame.valid) index.erase(frame.key);
        frame.key = key;
        frame.valid = true;
        frame.usage = 1;
        index[key] = slot;
    }

    void dump(std::ostream& os) const {
        std::lock_guard<std::mutex> lock(cacheMutex);
        os << "[hand=" << clockHand << "] ";
        for (std::size_t i = 0; i < frames.size(); ++i) {
            const Frame& frame = frames[i];
            os << i << ":";
            if (frame.valid) os << frame.key << "/u" << static_cast<int>(frame.usage);
            else os << "-";
            os << " ";
        }
        os << "\n";
    }

private:
    static constexpr std::uint8_t kMaxUsage = 5;

    struct Frame {
        T key{};
        std::uint8_t usage{0};
        bool valid{false};
    };

    std::size_t pickVictim() {
        const std::size_t numFrames = frames.size();
        for (std::size_t spin = 0; spin < numFrames * (kMaxUsage + 1); ++spin) {
            Frame& frame = frames[clockHand];
            if (!frame.valid || frame.usage == 0) {
                std::size_t chosen = clockHand;
                clockHand = (clockHand + 1) % numFrames;
                return chosen;
            }
            frame.usage--;
            clockHand = (clockHand + 1) % numFrames;
        }
        std::size_t chosen = clockHand;
        clockHand = (clockHand + 1) % numFrames;
        return chosen;
    }

    void sweepLoop() {
        std::unique_lock<std::mutex> lock(cacheMutex);
        while (!stopFlag) {
            conditionVariable.wait_for(lock, std::chrono::milliseconds(50), [&] { return stopFlag.load(); });
            if (stopFlag) break;
            for (auto& frame : frames) {
                if (frame.valid && frame.usage > 0) frame.usage--;
            }
        }
    }

    std::size_t maxCacheSize;
    std::vector<Frame> frames;
    std::unordered_map<T, std::size_t> index;
    std::size_t clockHand;
    mutable std::mutex cacheMutex;
    std::condition_variable conditionVariable;
    std::atomic<bool> stopFlag;
    std::thread backgroundClockThread;
};

int main() {
    ClockSweep<int> cache(4);

    // Fill cache with initial keys
    for (int key : {10, 20, 30, 40}) {
        cache.putKey(key);
    }
    std::cout << "After filling 10, 20, 30, 40:\n";
    cache.dump(std::cout);

    // Access some keys to increase usage counts
    cache.getKey(10);
    cache.getKey(10);
    cache.getKey(30);
    std::cout << "After hitting 10 twice, 30 once:\n";
    cache.dump(std::cout);

    // Insert new keys (should evict lowest usage frames)
    cache.putKey(50);
    cache.putKey(60);
    std::cout << "After inserting 50, 60 (20 and 40 should be victims):\n";
    cache.dump(std::cout);

    // Test lookups
    std::cout << "\nLookup results:\n";
    std::cout << "  lookup 20 -> " << (cache.getKey(20) ? "hit" : "miss") << "\n";
    std::cout << "  lookup 10 -> " << (cache.getKey(10) ? "hit" : "miss") << "\n";
    std::cout << "  lookup 50 -> " << (cache.getKey(50) ? "hit" : "miss") << "\n";

    // Wait for background sweep to cool down usage counts
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    std::cout << "\nAfter background sweep cools usage counts:\n";
    cache.dump(std::cout);

    return 0;
}