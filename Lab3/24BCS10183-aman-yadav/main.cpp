// Lab 3 — Clock Sweep buffer cache
// Author: 24BCS10183 Aman Yadav  (Class B, 2nd year)
//
// A generic CLOCK / second-chance page-replacement cache with a background
// "aging" thread. Public API mirrors the instructor's skeleton in
// storage_buffer/main.cpp: getKey / putKey.
//
//   ClockSweepCache<T>(capacity, sweepInterval)
//     T    getKey(const T& key)   -> on hit, sets ref bit and returns the key;
//                                    on miss returns T{}.
//     void putKey(const T& key)   -> inserts or refreshes; on a full cache,
//                                    selects a victim via the clock sweep.
//
// Designed so the same code can later hold real Page objects: just
// instantiate ClockSweepCache<Page>.

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

template <typename T>
class ClockSweepCache {
public:
    explicit ClockSweepCache(std::size_t capacity,
                             std::chrono::milliseconds sweepInterval =
                                 std::chrono::milliseconds(500))
        : capacity_(capacity),
          sweepInterval_(sweepInterval),
          frames_(capacity),
          hand_(0),
          stop_(false) {
        if (capacity == 0) {
            throw std::invalid_argument("ClockSweepCache capacity must be > 0");
        }
        sweeper_ = std::thread(&ClockSweepCache::sweepLoop, this);
    }

    ~ClockSweepCache() {
        {
            std::lock_guard<std::mutex> lk(mu_);
            stop_ = true;
        }
        cv_.notify_all();
        if (sweeper_.joinable()) sweeper_.join();
    }

    ClockSweepCache(const ClockSweepCache&) = delete;
    ClockSweepCache& operator=(const ClockSweepCache&) = delete;

    // On hit: set ref bit and return the cached value.
    // On miss: return a default-constructed T.
    T getKey(const T& key) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = index_.find(key);
        if (it == index_.end()) return T{};
        frames_[it->second].ref = true;
        return frames_[it->second].key;
    }

    // Insert key. If already cached, refresh the ref bit and return.
    // If a free slot exists, place it there. Otherwise run the clock sweep
    // and overwrite the chosen victim.
    void putKey(const T& key) {
        std::lock_guard<std::mutex> lk(mu_);

        auto it = index_.find(key);
        if (it != index_.end()) {
            frames_[it->second].ref = true;
            return;
        }

        // Try one full lap for a free slot first.
        for (std::size_t scanned = 0; scanned < frames_.size(); ++scanned) {
            if (!frames_[hand_].occupied) {
                frames_[hand_] = Frame{key, true, true};
                index_[key] = hand_;
                hand_ = (hand_ + 1) % frames_.size();
                return;
            }
            hand_ = (hand_ + 1) % frames_.size();
        }

        // No free slot: clock-sweep for a victim.
        std::size_t victim = pickVictimLocked();
        index_.erase(frames_[victim].key);
        frames_[victim] = Frame{key, true, true};
        index_[key] = victim;
        hand_ = (victim + 1) % frames_.size();
    }

    bool contains(const T& key) {
        std::lock_guard<std::mutex> lk(mu_);
        return index_.find(key) != index_.end();
    }

    std::size_t size() {
        std::lock_guard<std::mutex> lk(mu_);
        return index_.size();
    }

    std::size_t capacity() const { return capacity_; }

    void debugPrint(const std::string& label) {
        std::lock_guard<std::mutex> lk(mu_);
        std::cout << "[" << label << "] hand=" << hand_ << " | ";
        for (std::size_t i = 0; i < frames_.size(); ++i) {
            std::cout << "slot" << i << "=";
            if (frames_[i].occupied) {
                std::cout << frames_[i].key
                          << "(ref=" << (frames_[i].ref ? '1' : '0') << ")";
            } else {
                std::cout << "_";
            }
            if (i + 1 < frames_.size()) std::cout << "  ";
        }
        std::cout << "\n";
    }

private:
    struct Frame {
        T key{};
        bool ref{false};
        bool occupied{false};
    };

    std::size_t pickVictimLocked() {
        // Worst case: two laps — first lap clears all ref bits, second lap
        // is guaranteed to find a refBit=0 victim.
        for (std::size_t step = 0; step < 2 * frames_.size(); ++step) {
            Frame& f = frames_[hand_];
            if (f.occupied && !f.ref) return hand_;
            if (f.occupied && f.ref) f.ref = false;
            hand_ = (hand_ + 1) % frames_.size();
        }
        return hand_;
    }

    void sweepLoop() {
        std::unique_lock<std::mutex> lk(mu_);
        while (!stop_) {
            // wait_for returns true if predicate became true (i.e. we're
            // shutting down) — exit promptly instead of finishing one more
            // useless sweep cycle.
            if (cv_.wait_for(lk, sweepInterval_, [this] { return stop_; })) {
                break;
            }
            for (auto& f : frames_) {
                if (f.occupied && f.ref) f.ref = false;
            }
        }
    }

    std::size_t capacity_;
    std::chrono::milliseconds sweepInterval_;
    std::vector<Frame> frames_;
    std::unordered_map<T, std::size_t> index_;
    std::size_t hand_;

    std::mutex mu_;
    std::condition_variable cv_;
    bool stop_;
    std::thread sweeper_;
};

namespace {

void demoInts() {
    std::cout << "=== Demo 1: ClockSweepCache<int>, capacity=4, sweep=300ms ===\n";
    ClockSweepCache<int> cache(4, std::chrono::milliseconds(300));

    cache.putKey(1); cache.debugPrint("put 1");
    cache.putKey(2); cache.debugPrint("put 2");
    cache.putKey(3); cache.debugPrint("put 3");
    cache.putKey(4); cache.debugPrint("put 4 (cache full)");

    std::cout << "\n-- sleep 400ms so the background sweep ages all ref bits to 0 --\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    cache.debugPrint("after bg sweep");

    cache.getKey(2); cache.debugPrint("get 2 (hit, refBit -> 1)");
    cache.getKey(4); cache.debugPrint("get 4 (hit, refBit -> 1)");

    std::cout << "\n-- put 5: clock should evict the first slot with refBit=0 --\n";
    cache.putKey(5); cache.debugPrint("put 5");

    std::cout << "\n-- put 6: another eviction via clock sweep --\n";
    cache.putKey(6); cache.debugPrint("put 6");

    std::cout << "\ncontains(2)=" << std::boolalpha << cache.contains(2)
              << "  contains(1)=" << cache.contains(1)
              << "  contains(5)=" << cache.contains(5) << "\n";
    std::cout << "size=" << cache.size()
              << " / capacity=" << cache.capacity() << "\n\n";
}

void demoStrings() {
    std::cout << "=== Demo 2: ClockSweepCache<std::string>, capacity=3 ===\n";
    ClockSweepCache<std::string> sc(3, std::chrono::milliseconds(500));

    sc.putKey("alpha");
    sc.putKey("bravo");
    sc.putKey("charlie");
    sc.debugPrint("filled");

    sc.getKey("alpha");
    sc.debugPrint("get alpha (refBit -> 1)");

    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    sc.debugPrint("after bg sweep");

    sc.getKey("alpha");
    sc.debugPrint("get alpha again (only alpha has ref=1)");

    sc.putKey("delta");
    sc.debugPrint("put delta (evicts bravo — first refBit=0 slot)");

    sc.putKey("echo");
    sc.debugPrint("put echo");

    std::cout << "contains(alpha)=" << sc.contains("alpha")
              << "  contains(bravo)=" << sc.contains("bravo")
              << "  contains(charlie)=" << sc.contains("charlie") << "\n\n";
}

void demoRefresh() {
    std::cout << "=== Demo 3: putKey on existing key just refreshes refBit ===\n";
    ClockSweepCache<int> c(3, std::chrono::milliseconds(1000));
    c.putKey(100); c.putKey(200); c.putKey(300);
    c.debugPrint("filled");
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    c.debugPrint("after bg sweep (all aged)");
    c.putKey(200);
    c.debugPrint("put 200 again (existing key, refBit -> 1)");
    std::cout << "size=" << c.size() << " (still 3, no growth)\n\n";
}

}  // namespace

int main() {
    demoInts();
    demoStrings();
    demoRefresh();
    std::cout << "All demos finished.\n";
    return 0;
}
