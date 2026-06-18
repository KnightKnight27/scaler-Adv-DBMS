// Lab 3 - Clock Sweep buffer replacement
// Siddhanth Kapoor (10154)

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
        : maxCacheSize(maxNumber), frames(maxNumber), hand(0), stopFlag(false) {
        bgClockThread = std::thread(&ClockSweep::sweepLoop, this);
    }

    ~ClockSweep() {
        {
            std::lock_guard<std::mutex> lk(mu);
            stopFlag = true;
        }
        cv.notify_all();
        if (bgClockThread.joinable()) bgClockThread.join();
    }

    ClockSweep(const ClockSweep&) = delete;
    ClockSweep& operator=(const ClockSweep&) = delete;

    bool getKey(const T& key) {
        std::lock_guard<std::mutex> lk(mu);
        auto it = index.find(key);
        if (it == index.end()) return false;
        Frame& f = frames[it->second];
        if (f.usage < kMaxUsage) f.usage++;
        return true;
    }

    void putKey(const T& key) {
        std::lock_guard<std::mutex> lk(mu);
        if (index.count(key)) {
            Frame& f = frames[index[key]];
            if (f.usage < kMaxUsage) f.usage++;
            return;
        }
        std::size_t slot = pickVictim();
        Frame& f = frames[slot];
        if (f.valid) index.erase(f.key);
        f.key = key;
        f.valid = true;
        f.usage = 1;
        index[key] = slot;
    }

    void dump(std::ostream& os) const {
        std::lock_guard<std::mutex> lk(mu);
        os << "[hand=" << hand << "] ";
        for (std::size_t i = 0; i < frames.size(); ++i) {
            const Frame& f = frames[i];
            os << i << ":";
            if (f.valid) os << f.key << "/u" << static_cast<int>(f.usage);
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
        const std::size_t n = frames.size();
        for (std::size_t spin = 0; spin < n * (kMaxUsage + 1); ++spin) {
            Frame& f = frames[hand];
            if (!f.valid || f.usage == 0) {
                std::size_t chosen = hand;
                hand = (hand + 1) % n;
                return chosen;
            }
            f.usage--;
            hand = (hand + 1) % n;
        }
        std::size_t chosen = hand;
        hand = (hand + 1) % n;
        return chosen;
    }

    void sweepLoop() {
        std::unique_lock<std::mutex> lk(mu);
        while (!stopFlag) {
            cv.wait_for(lk, std::chrono::milliseconds(50), [&] { return stopFlag.load(); });
            if (stopFlag) break;
            for (auto& f : frames) {
                if (f.valid && f.usage > 0) f.usage--;
            }
        }
    }

    std::size_t maxCacheSize;
    std::vector<Frame> frames;
    std::unordered_map<T, std::size_t> index;
    std::size_t hand;
    mutable std::mutex mu;
    std::condition_variable cv;
    std::atomic<bool> stopFlag;
    std::thread bgClockThread;
};

int main() {
    ClockSweep<int> cache(4);

    for (int k : {10, 20, 30, 40}) cache.putKey(k);
    std::cout << "after filling 10,20,30,40\n";
    cache.dump(std::cout);

    cache.getKey(10);
    cache.getKey(10);
    cache.getKey(30);
    std::cout << "after hitting 10 twice, 30 once\n";
    cache.dump(std::cout);

    cache.putKey(50);
    cache.putKey(60);
    std::cout << "after inserting 50, 60 (20 and 40 should be victims)\n";
    cache.dump(std::cout);

    std::cout << "lookup 20 -> " << (cache.getKey(20) ? "hit" : "miss") << "\n";
    std::cout << "lookup 10 -> " << (cache.getKey(10) ? "hit" : "miss") << "\n";
    std::cout << "lookup 50 -> " << (cache.getKey(50) ? "hit" : "miss") << "\n";

    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    std::cout << "after background sweep cools usage counts\n";
    cache.dump(std::cout);

    return 0;
}
