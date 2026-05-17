// Lab 3 — Clock Sweep Buffer Cache Implementation
// Author: 24BCS10406 Manasvi Sabbarwal
//
// Implements the CLOCK (second-chance) page replacement algorithm as a
// templated buffer cache. Includes a background sweeper thread for
// periodic reference-bit aging.
//
// API:
//   ClockSweep<T>(capacity, sweepIntervalMs)
//     T    getKey(const T& key)   — cache hit: returns key, sets refBit; miss: returns T{}
//     void putKey(const T& key)   — insert or refresh; evicts via clock sweep when full
//

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

template <typename T>
class ClockSweep {
public:
    explicit ClockSweep(std::size_t capacity,
                        std::chrono::milliseconds sweepIntervalMs =
                            std::chrono::milliseconds(500))
        : capacity_(capacity),
          sweepInterval_(sweepIntervalMs),
          buffer_(capacity),
          clockHand_(0),
          running_(true) {
        if (capacity == 0) {
            throw std::invalid_argument("Cache capacity must be greater than 0");
        }
        sweeper_ = std::thread(&ClockSweep::backgroundSweep, this);
    }

    ~ClockSweep() {
        {
            std::lock_guard<std::mutex> guard(mtx_);
            running_ = false;
        }
        wakeup_.notify_all();
        if (sweeper_.joinable()) {
            sweeper_.join();
        }
    }

    // Non-copyable, non-movable
    ClockSweep(const ClockSweep&) = delete;
    ClockSweep& operator=(const ClockSweep&) = delete;
    ClockSweep(ClockSweep&&) = delete;
    ClockSweep& operator=(ClockSweep&&) = delete;

    // Returns the cached key on hit (and marks it as recently used).
    // Returns default-constructed T{} on miss.
    T getKey(const T& key) {
        std::lock_guard<std::mutex> guard(mtx_);
        auto found = lookup_.find(key);
        if (found == lookup_.end()) {
            return T{};  // cache miss
        }
        buffer_[found->second].referenced = true;
        return buffer_[found->second].data;
    }

    // Inserts a key into the cache. If the key already exists, just
    // refreshes its reference bit. If the buffer is full, runs the
    // clock-sweep algorithm to find a victim for eviction.
    void putKey(const T& key) {
        std::lock_guard<std::mutex> guard(mtx_);

        // Already in cache — just refresh
        auto found = lookup_.find(key);
        if (found != lookup_.end()) {
            buffer_[found->second].referenced = true;
            return;
        }

        // Try to find an empty frame first
        for (std::size_t i = 0; i < buffer_.size(); ++i) {
            std::size_t idx = (clockHand_ + i) % buffer_.size();
            if (!buffer_[idx].valid) {
                buffer_[idx] = Frame{key, true, true};
                lookup_[key] = idx;
                clockHand_ = (idx + 1) % buffer_.size();
                return;
            }
        }

        // Buffer full — run clock sweep to find victim
        std::size_t victim = evictVictim();
        lookup_.erase(buffer_[victim].data);
        buffer_[victim] = Frame{key, true, true};
        lookup_[key] = victim;
        clockHand_ = (victim + 1) % buffer_.size();
    }

    bool contains(const T& key) {
        std::lock_guard<std::mutex> guard(mtx_);
        return lookup_.count(key) > 0;
    }

    std::size_t size() {
        std::lock_guard<std::mutex> guard(mtx_);
        return lookup_.size();
    }

    std::size_t capacity() const { return capacity_; }

    // Print the current state of the buffer for debugging
    void dump(const std::string& tag) {
        std::lock_guard<std::mutex> guard(mtx_);
        std::cout << "[" << tag << "] hand=" << clockHand_ << " |";
        for (std::size_t i = 0; i < buffer_.size(); ++i) {
            std::cout << " f" << i << "=";
            if (buffer_[i].valid) {
                std::cout << buffer_[i].data
                          << "(r=" << (buffer_[i].referenced ? 1 : 0) << ")";
            } else {
                std::cout << "empty";
            }
        }
        std::cout << "\n";
    }

private:
    struct Frame {
        T data{};
        bool referenced{false};
        bool valid{false};
    };

    std::size_t capacity_;
    std::chrono::milliseconds sweepInterval_;
    std::vector<Frame> buffer_;
    std::unordered_map<T, std::size_t> lookup_;
    std::size_t clockHand_;

    std::mutex mtx_;
    std::condition_variable wakeup_;
    bool running_;
    std::thread sweeper_;

    // Finds a victim frame using the clock/second-chance algorithm.
    // Must be called with mtx_ held.
    std::size_t evictVictim() {
        // Scan at most 2 full rotations (guarantees a victim is found
        // since the second pass clears all remaining ref bits)
        for (std::size_t n = 0; n < 2 * buffer_.size(); ++n) {
            Frame& f = buffer_[clockHand_];
            if (f.valid && !f.referenced) {
                return clockHand_;
            }
            if (f.valid && f.referenced) {
                f.referenced = false;  // second chance
            }
            clockHand_ = (clockHand_ + 1) % buffer_.size();
        }
        // Fallback (should never reach here with correct logic)
        return clockHand_;
    }

    // Background thread: periodically clears all reference bits (aging).
    void backgroundSweep() {
        std::unique_lock<std::mutex> lock(mtx_);
        while (running_) {
            if (wakeup_.wait_for(lock, sweepInterval_,
                                 [this] { return !running_; })) {
                break;  // shutdown requested
            }
            // Age all reference bits
            for (auto& f : buffer_) {
                if (f.valid && f.referenced) {
                    f.referenced = false;
                }
            }
        }
    }
};

// ─────────────────────────────────────────────────────────────────────
// Demo / test harness
// ─────────────────────────────────────────────────────────────────────

namespace {

void demo_integer_cache() {
    std::cout << "=== Demo 1: Integer cache, capacity=4, sweep=300ms ===\n";
    ClockSweep<int> cache(4, std::chrono::milliseconds(300));

    cache.putKey(10); cache.dump("put 10");
    cache.putKey(20); cache.dump("put 20");
    cache.putKey(30); cache.dump("put 30");
    cache.putKey(40); cache.dump("put 40 — full");

    std::cout << "\n-- waiting 400ms for background sweep to age ref bits --\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    cache.dump("after sweep");

    // Touch 20 and 40 — they should survive eviction
    cache.getKey(20); cache.dump("get 20 — ref set");
    cache.getKey(40); cache.dump("get 40 — ref set");

    std::cout << "\n-- inserting 50: should evict first frame with ref=0 --\n";
    cache.putKey(50); cache.dump("put 50");

    std::cout << "\n-- inserting 60: next eviction via clock sweep --\n";
    cache.putKey(60); cache.dump("put 60");

    std::cout << "\ncontains(20)=" << std::boolalpha << cache.contains(20)
              << "  contains(10)=" << cache.contains(10)
              << "  contains(50)=" << cache.contains(50) << "\n";
    std::cout << "size=" << cache.size()
              << " / capacity=" << cache.capacity() << "\n\n";
}

void demo_string_cache() {
    std::cout << "=== Demo 2: String cache, capacity=3, sweep=500ms ===\n";
    ClockSweep<std::string> cache(3, std::chrono::milliseconds(500));

    cache.putKey("red");
    cache.putKey("green");
    cache.putKey("blue");
    cache.dump("filled");

    cache.getKey("red");
    cache.dump("get red — ref set");

    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    cache.dump("after sweep — only red was re-touched");

    cache.getKey("red");
    cache.dump("get red again — only red has ref=1");

    cache.putKey("yellow");
    cache.dump("put yellow — evicts first ref=0 frame");

    cache.putKey("purple");
    cache.dump("put purple");

    std::cout << "contains(red)=" << std::boolalpha << cache.contains("red")
              << "  contains(green)=" << cache.contains("green")
              << "  contains(blue)=" << cache.contains("blue") << "\n\n";
}

void demo_duplicate_put() {
    std::cout << "=== Demo 3: Duplicate putKey refreshes ref bit, no growth ===\n";
    ClockSweep<int> cache(3, std::chrono::milliseconds(1000));

    cache.putKey(100); cache.putKey(200); cache.putKey(300);
    cache.dump("filled");

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    cache.dump("after sweep — all aged");

    cache.putKey(200);  // should just refresh, not insert
    cache.dump("put 200 again — ref refreshed");
    std::cout << "size=" << cache.size() << " (unchanged)\n\n";
}

}  // namespace

int main() {
    demo_integer_cache();
    demo_string_cache();
    demo_duplicate_put();
    std::cout << "All demos completed.\n";
    return 0;
}
