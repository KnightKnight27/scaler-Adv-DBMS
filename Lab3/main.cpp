// Lab 3 - Clock Sweep Buffer Cache Implementation
// Author: 24BCS10345 Ansh Mahajan
//
// Implements CLOCK (second-chance) page replacement as a templated
// buffer cache. A background sweeper thread periodically ages
// reference bits.
//
// API:
//   ClockSweep<T>(capacity, sweepIntervalMs)
//     T    getKey(const T& key)   - hit: returns key, sets refBit; miss: returns T{}
//     void putKey(const T& key)   - insert or refresh; evicts via clock sweep when full
//

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
class ClockSweep {
public:
    explicit ClockSweep(std::size_t capacity,
                        std::chrono::milliseconds sweepIntervalMs =
                            std::chrono::milliseconds(500))
        : capacity_(capacity),
          sweepPeriod_(sweepIntervalMs),
          frames_(capacity),
          hand_(0),
          isRunning_(true) {
        if (capacity == 0) {
            throw std::invalid_argument("Cache capacity must be greater than 0");
        }
        sweeperThread_ = std::thread(&ClockSweep::sweeperLoop, this);
    }

    ~ClockSweep() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            isRunning_ = false;
        }
        cv_.notify_all();
        if (sweeperThread_.joinable()) {
            sweeperThread_.join();
        }
    }

    // No copy or move; cache owns a worker thread.
    ClockSweep(const ClockSweep&) = delete;
    ClockSweep& operator=(const ClockSweep&) = delete;
    ClockSweep(ClockSweep&&) = delete;
    ClockSweep& operator=(ClockSweep&&) = delete;

    // On hit, returns key and marks it as recently used.
    // On miss, returns T{} (default).
    T getKey(const T& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = index_.find(key);
        if (it == index_.end()) {
            return T{};  // cache miss
        }
        frames_[it->second].refBit = true;
        return frames_[it->second].payload;
    }

    // Inserts a key into the cache. If the key already exists, just
    // refreshes its reference bit. If the buffer is full, runs the
    // clock-sweep algorithm to find a victim for eviction.
    void putKey(const T& key) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Already cached - refresh the ref bit only.
        auto it = index_.find(key);
        if (it != index_.end()) {
            frames_[it->second].refBit = true;
            return;
        }

        // Prefer an empty frame before evicting.
        for (std::size_t i = 0; i < frames_.size(); ++i) {
            std::size_t idx = (hand_ + i) % frames_.size();
            if (!frames_[idx].inUse) {
                frames_[idx] = Frame{key, true, true};
                index_[key] = idx;
                hand_ = (idx + 1) % frames_.size();
                return;
            }
        }

        // Buffer full - choose a victim via clock sweep.
        std::size_t victimIdx = pickVictim();
        index_.erase(frames_[victimIdx].payload);
        frames_[victimIdx] = Frame{key, true, true};
        index_[key] = victimIdx;
        hand_ = (victimIdx + 1) % frames_.size();
    }

    bool contains(const T& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        return index_.count(key) > 0;
    }

    std::size_t size() {
        std::lock_guard<std::mutex> lock(mutex_);
        return index_.size();
    }

    std::size_t capacity() const { return capacity_; }

    // Debug dump of current buffer state.
    void dump(const std::string& label) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::cout << "[" << label << "] hand=" << hand_ << " |";
        for (std::size_t i = 0; i < frames_.size(); ++i) {
            std::cout << " f" << i << "=";
            if (frames_[i].inUse) {
                std::cout << frames_[i].payload
                          << "(r=" << (frames_[i].refBit ? 1 : 0) << ")";
            } else {
                std::cout << "empty";
            }
        }
        std::cout << "\n";
    }

private:
    struct Frame {
        T payload{};
        bool refBit{false};
        bool inUse{false};
    };

    std::size_t capacity_;
    std::chrono::milliseconds sweepPeriod_;
    std::vector<Frame> frames_;
    std::unordered_map<T, std::size_t> index_;
    std::size_t hand_;

    std::mutex mutex_;
    std::condition_variable cv_;
    bool isRunning_;
    std::thread sweeperThread_;

    // Finds a victim frame using the clock/second-chance algorithm.
    // Requires mutex_ held.
    std::size_t pickVictim() {
        // Scan at most two full rotations; the second pass clears
        // remaining ref bits.
        for (std::size_t n = 0; n < 2 * frames_.size(); ++n) {
            Frame& f = frames_[hand_];
            if (f.inUse && !f.refBit) {
                return hand_;
            }
            if (f.inUse && f.refBit) {
                f.refBit = false;  // grant second chance
            }
            hand_ = (hand_ + 1) % frames_.size();
        }
        // Fallback (defensive).
        return hand_;
    }

    // Background thread: periodically ages reference bits.
    void sweeperLoop() {
        std::unique_lock<std::mutex> lock(mutex_);
        while (isRunning_) {
            if (cv_.wait_for(lock, sweepPeriod_,
                             [this] { return !isRunning_; })) {
                break;  // shutdown requested
            }
            // Age reference bits.
            for (auto& f : frames_) {
                if (f.inUse && f.refBit) {
                    f.refBit = false;
                }
            }
        }
    }
};

// ---------------------------------------------------------------------
// Demo / test harness
// ---------------------------------------------------------------------

namespace {

void run_integer_demo() {
    std::cout << "=== Demo 1: Integer cache, capacity=4, sweep=300ms ===\n";
    ClockSweep<int> buf(4, std::chrono::milliseconds(300));

    buf.putKey(10); buf.dump("put 10");
    buf.putKey(20); buf.dump("put 20");
    buf.putKey(30); buf.dump("put 30");
    buf.putKey(40); buf.dump("put 40 (full)");

    std::cout << "\n-- waiting 400ms for background sweep to age ref bits --\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    buf.dump("after sweep");

    // Touch 20 and 40 to keep them hot.
    buf.getKey(20); buf.dump("get 20 (ref set)");
    buf.getKey(40); buf.dump("get 40 (ref set)");

    std::cout << "\n-- inserting 50: should evict first frame with ref=0 --\n";
    buf.putKey(50); buf.dump("put 50");

    std::cout << "\n-- inserting 60: next eviction via clock sweep --\n";
    buf.putKey(60); buf.dump("put 60");

    std::cout << "\ncontains(20)=" << std::boolalpha << buf.contains(20)
              << "  contains(10)=" << buf.contains(10)
              << "  contains(50)=" << buf.contains(50) << "\n";
    std::cout << "size=" << buf.size()
              << " / capacity=" << buf.capacity() << "\n\n";
}

void run_string_demo() {
    std::cout << "=== Demo 2: String cache, capacity=3, sweep=500ms ===\n";
    ClockSweep<std::string> buf(3, std::chrono::milliseconds(500));

    buf.putKey("red");
    buf.putKey("green");
    buf.putKey("blue");
    buf.dump("filled");

    buf.getKey("red");
    buf.dump("get red (ref set)");

    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    buf.dump("after sweep (only red re-touched)");

    buf.getKey("red");
    buf.dump("get red again (only red has ref=1)");

    buf.putKey("yellow");
    buf.dump("put yellow (evicts first ref=0 frame)");

    buf.putKey("purple");
    buf.dump("put purple");

    std::cout << "contains(red)=" << std::boolalpha << buf.contains("red")
              << "  contains(green)=" << buf.contains("green")
              << "  contains(blue)=" << buf.contains("blue") << "\n\n";
}

void run_duplicate_demo() {
    std::cout << "=== Demo 3: Duplicate putKey refreshes ref bit, no growth ===\n";
    ClockSweep<int> buf(3, std::chrono::milliseconds(1000));

    buf.putKey(100); buf.putKey(200); buf.putKey(300);
    buf.dump("filled");

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    buf.dump("after sweep (all aged)");

    buf.putKey(200);  // refresh only; no insert
    buf.dump("put 200 again (ref refreshed)");
    std::cout << "size=" << buf.size() << " (unchanged)\n\n";
}

}  // namespace

int main() {
    run_integer_demo();
    run_string_demo();
    run_duplicate_demo();
    std::cout << "All demos completed.\n";
    return 0;
}