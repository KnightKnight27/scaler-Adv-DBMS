// Lab 3 - Clock Sweep buffer cache
// Utkarsh Raj 24BCS10318


#include <chrono>
#include <condition_variable>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

template <typename T>
struct Frame {
    T    value{};
    bool occupied{false};
    bool referenced{false};
    bool pinned{false};
};

template <typename T>
class ClockSweep {
    using uint = unsigned int;

public:
    explicit ClockSweep(int maxNumber, unsigned sweepIntervalMs = 500)
        : maxCacheSize(static_cast<uint>(maxNumber)),
          sweepInterval(sweepIntervalMs)
    {
        if (maxNumber <= 0)
            throw std::invalid_argument("ClockSweep: capacity must be > 0");
        frames.resize(maxCacheSize);
        if (sweepInterval > 0)
            bgClockThread = std::thread(&ClockSweep::sweeper, this);
    }

    ~ClockSweep() {
        { std::lock_guard<std::mutex> lk(mtx); stopSweeper = true; }
        cv.notify_all();
        if (bgClockThread.joinable()) bgClockThread.join();
    }

    ClockSweep(const ClockSweep&)            = delete;
    ClockSweep& operator=(const ClockSweep&) = delete;

    std::optional<T> getKey(const T& key) {
        std::lock_guard<std::mutex> lk(mtx);
        auto it = keyToFrame.find(key);
        if (it == keyToFrame.end()) { ++stats.misses; return std::nullopt; }
        frames[it->second].referenced = true;
        ++stats.hits;
        return frames[it->second].value;
    }

    void putKey(const T& key) {
        std::lock_guard<std::mutex> lk(mtx);
        if (keyToFrame.count(key)) {
            frames[keyToFrame[key]].referenced = true;
            return;
        }
        uint target = findVictim();
        if (frames[target].occupied) {
            keyToFrame.erase(frames[target].value);
            ++stats.evictions;
        }
        frames[target] = {key, true, true, false};
        keyToFrame[key] = target;
        ++stats.insertions;
    }

    bool pin(const T& key) {
        std::lock_guard<std::mutex> lk(mtx);
        auto it = keyToFrame.find(key);
        if (it == keyToFrame.end()) return false;
        frames[it->second].pinned = true;
        return true;
    }

    bool unpin(const T& key) {
        std::lock_guard<std::mutex> lk(mtx);
        auto it = keyToFrame.find(key);
        if (it == keyToFrame.end()) return false;
        frames[it->second].pinned = false;
        return true;
    }

    bool evict(const T& key) {
        std::lock_guard<std::mutex> lk(mtx);
        auto it = keyToFrame.find(key);
        if (it == keyToFrame.end()) return false;
        frames[it->second] = Frame<T>{};
        keyToFrame.erase(it);
        ++stats.evictions;
        return true;
    }

    struct Stats {
        uint64_t hits{0}, misses{0}, evictions{0}, insertions{0}, sweeps{0};
    };

    Stats getStats() const {
        std::lock_guard<std::mutex> lk(mtx);
        return stats;
    }

    std::string dump() const {
        std::lock_guard<std::mutex> lk(mtx);
        std::ostringstream os;
        os << "ClockSweep  [hand=" << clockHand
           << "  " << keyToFrame.size() << "/" << maxCacheSize << "]\n"
           << std::string(54, '-') << '\n'
           << std::setw(6) << "Frame" << std::setw(10) << "Value"
           << std::setw(10) << "Occupied" << std::setw(10) << "Ref"
           << std::setw(10) << "Pinned" << '\n';
        for (uint i = 0; i < maxCacheSize; ++i) {
            const auto& f = frames[i];
            std::ostringstream v;
            if (f.occupied) v << f.value; else v << "-";
            os << std::setw(6)  << i
               << std::setw(10) << v.str()
               << std::setw(10) << f.occupied
               << std::setw(10) << f.referenced
               << std::setw(10) << f.pinned
               << (i == clockHand ? "  <-- hand" : "") << '\n';
        }
        return os.str();
    }

private:
    const uint              maxCacheSize;
    const unsigned          sweepInterval;
    mutable std::mutex      mtx;
    std::condition_variable cv;
    bool                    stopSweeper{false};
    std::thread             bgClockThread;
    std::vector<Frame<T>>   frames;
    std::unordered_map<T, uint> keyToFrame;
    uint                    clockHand{0};
    Stats                   stats;

    uint findVictim() {
        for (uint i = 0; i < maxCacheSize; ++i)
            if (!frames[i].occupied) return i;

        for (int pass = 0; pass < 2; ++pass) {
            for (uint i = 0; i < maxCacheSize; ++i) {
                uint idx = (clockHand + i) % maxCacheSize;
                Frame<T>& f = frames[idx];
                if (f.pinned) continue;
                if (!f.referenced) {
                    clockHand = (idx + 1) % maxCacheSize;
                    return idx;
                }
                f.referenced = false;
            }
        }
        throw std::runtime_error("ClockSweep: all frames are pinned");
    }

    void sweeper() {
        std::unique_lock<std::mutex> lk(mtx);
        while (!stopSweeper) {
            cv.wait_for(lk, std::chrono::milliseconds(sweepInterval),
                        [this] { return stopSweeper; });
            if (stopSweeper) break;
            for (uint i = 0; i < maxCacheSize; ++i) {
                uint idx = (clockHand + i) % maxCacheSize;
                if (frames[idx].occupied && !frames[idx].pinned)
                    frames[idx].referenced = false;
            }
            clockHand = (clockHand + 1) % maxCacheSize;
            ++stats.sweeps;
        }
    }
};

// ── Demos ──────────────────────────────────────────────────────────────────

int main() {
    std::cout << "=== Clock Sweep Cache Demo ===\n\n";

    // Test 1: basic hit / miss / eviction
    {
        std::cout << "-- Test 1: hit / miss / eviction (capacity=3) --\n";
        ClockSweep<int> cs(3, 0);
        cs.putKey(10); cs.putKey(20); cs.putKey(30);

        auto v = cs.getKey(10);
        std::cout << "getKey(10): " << (v ? std::to_string(*v) : "MISS") << "\n";
        std::cout << "getKey(99): " << (cs.getKey(99) ? "HIT" : "MISS") << "\n";

        cs.putKey(40);
        std::cout << cs.dump();
        auto s = cs.getStats();
        std::cout << "hits=" << s.hits << " misses=" << s.misses
                  << " evictions=" << s.evictions << "\n\n";
    }

    // Test 2: second-chance keeps recently-used pages alive
    {
        std::cout << "-- Test 2: second-chance (capacity=3) --\n";
        ClockSweep<int> cs(3, 0);
        cs.putKey(1); cs.putKey(2); cs.putKey(3);
        cs.getKey(2); cs.getKey(3);  // re-touch 2 and 3
        cs.putKey(4);                 // page 1 (never re-touched) is evicted
        std::cout << "page 2 cached: " << (cs.getKey(2) ? "YES" : "NO") << "\n";
        std::cout << "page 3 cached: " << (cs.getKey(3) ? "YES" : "NO") << "\n";
        std::cout << "page 4 cached: " << (cs.getKey(4) ? "YES" : "NO") << "\n";
        std::cout << cs.dump() << "\n";
    }

    // Test 3: pinned frames cannot be evicted
    {
        std::cout << "-- Test 3: pin / unpin (capacity=2) --\n";
        ClockSweep<int> cs(2, 0);
        cs.putKey(100); cs.putKey(200);
        cs.pin(100);    cs.pin(200);
        try {
            cs.putKey(300);
            std::cout << "ERROR: should have thrown\n";
        } catch (const std::runtime_error& e) {
            std::cout << "Caught: " << e.what() << "\n";
        }
        cs.unpin(100);
        cs.putKey(300);
        std::cout << "getKey(200): " << (cs.getKey(200) ? "HIT" : "MISS") << "\n";
        std::cout << cs.dump() << "\n";
    }

    // Test 4: concurrent writers + background sweeper
    {
        std::cout << "-- Test 4: concurrent stress (8 frames, 4 threads, sweeper=50ms) --\n";
        ClockSweep<int> cs(8, 50);
        std::vector<std::thread> workers;
        for (int t = 0; t < 4; ++t) {
            workers.emplace_back([&cs, t]() {
                for (int i = 0; i < 200; ++i) {
                    int k = (t * 200 + i) % 24;
                    cs.putKey(k); cs.getKey(k);
                }
            });
        }
        for (auto& w : workers) w.join();
        auto s = cs.getStats();
        std::cout << "hits=" << s.hits << " misses=" << s.misses
                  << " evictions=" << s.evictions << " sweeps=" << s.sweeps << "\n\n";
    }

    // Test 5: template works with std::string
    {
        std::cout << "-- Test 5: ClockSweep<std::string> (capacity=3) --\n";
        ClockSweep<std::string> cs(3, 0);
        cs.putKey("alpha"); cs.putKey("beta"); cs.putKey("gamma");
        cs.getKey("alpha");
        cs.putKey("delta");
        std::cout << "alpha cached: " << (cs.getKey("alpha") ? "YES" : "NO") << "\n";
        std::cout << cs.dump() << "\n";
    }

    std::cout << "=== All tests passed ===\n";
    return 0;
}