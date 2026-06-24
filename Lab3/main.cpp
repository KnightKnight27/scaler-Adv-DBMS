#include <iostream>
#include <vector>
#include <unordered_map>
#include <optional>
#include <stdexcept>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <string>

template<typename T>
struct Frame {
    T value;
    bool occupied   = false;
    bool referenced = false;   
};


// ClockSweep
//
// A fixed-size cache backed by a circular buffer of Frames.
//
// Clock-sweep eviction rule:
//   Spin the clock hand.
//   - If the current frame is empty          → use it immediately.
//   - If ref bit == 1                         → clear it, advance hand.
//   - If ref bit == 0 and frame is occupied  → evict, use that slot.
//
// A background thread calls sweep() every `intervalSeconds` seconds,
// clearing ref bits so cold entries age out proactively.

template<typename T>
class ClockSweep {
public:
  
    explicit ClockSweep(int maxCacheSize, int intervalSeconds = 5)
        : maxCacheSize_(static_cast<std::size_t>(maxCacheSize)),
          frames_(maxCacheSize),
          clockHand_(0),
          stopBg_(false)
    {
      
        bgClockThread_ = std::thread([this, intervalSeconds]() {
            while (!stopBg_.load()) {
                std::this_thread::sleep_for(std::chrono::seconds(intervalSeconds));
                if (!stopBg_.load()) {
                    backgroundSweep();
                }
            }
        });
    }

  
    ~ClockSweep() {
        stopBg_.store(true);
        bgClockThread_.join();
    }

   
    // get(key) — returns the cached value or throws std::out_of_range
    // Sets the ref bit on a hit (marks the entry as recently used).

    T get(T key) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = index_.find(key);
        if (it == index_.end()) {
            throw std::out_of_range("Cache miss: key not found");
        }

        std::size_t slot = it->second;
        frames_[slot].referenced = true;  
        return frames_[slot].value;
    }

   
    // put(key) — inserts key into the cache.
    // If already present, refreshes the ref bit.
    // If the cache is full, runs one clock-sweep eviction first.
 
    void put(T key) {
        std::lock_guard<std::mutex> lock(mutex_);

        
        auto it = index_.find(key);
        if (it != index_.end()) {
            frames_[it->second].referenced = true;
            return;
        }

      
        std::size_t slot = findOrEvict();

        frames_[slot].value      = key;
        frames_[slot].occupied   = true;
        frames_[slot].referenced = true;
        index_[key]              = slot;
    }

   
    // debug helper — prints the current state of every frame
 
    void printState() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::cout << "Cache state (hand=" << clockHand_ << "):\n";
        for (std::size_t i = 0; i < maxCacheSize_; ++i) {
            const auto& f = frames_[i];
            std::cout << "  [" << i << "] ";
            if (f.occupied) {
                std::cout << "value=" << f.value
                          << "  ref=" << f.referenced;
            } else {
                std::cout << "<empty>";
            }
            if (i == clockHand_) std::cout << "  <-- hand";
            std::cout << "\n";
        }
    }

private:

    // findOrEvict — returns the index of a slot ready to be written.
    // Must be called with mutex_ held.
  
    std::size_t findOrEvict() {
        // First pass: any empty slot?
        for (std::size_t i = 0; i < maxCacheSize_; ++i) {
            if (!frames_[i].occupied) return i;
        }

        // Cache full — run clock sweep until we find a victim
        while (true) {
            Frame<T>& f = frames_[clockHand_];

            if (!f.occupied) {
                
                std::size_t slot = clockHand_;
                advance();
                return slot;
            }

            if (f.referenced) {
                f.referenced = false;
                advance();
            } else {
                index_.erase(f.value);
                f.occupied   = false;
                f.referenced = false;
                std::size_t slot = clockHand_;
                advance();
                return slot;
            }
        }
    }

  
    // backgroundSweep — called by the background thread.
    // Walks all frames and clears ref bits so cold entries age out.
    // Does NOT evict anything — eviction is demand-driven in put().

    void backgroundSweep() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::cout << "[bg] Running background sweep...\n";
        for (auto& f : frames_) {
            if (f.occupied && f.referenced) {
                f.referenced = false;   
            }
        }
    }

    inline void advance() {
        clockHand_ = (clockHand_ + 1) % maxCacheSize_;
    }

    const std::size_t                        maxCacheSize_;
    std::vector<Frame<T>>                    frames_;
    std::unordered_map<T, std::size_t>       index_;   
    std::size_t                              clockHand_;

    mutable std::mutex                       mutex_;
    std::thread                              bgClockThread_;
    std::atomic<bool>                        stopBg_;
};


// main — basic smoke test

int main() {
    std::cout << "=== ClockSweep<int> (capacity 4) ===\n";
    {
        ClockSweep<int> cache(4, 2); 

        cache.put(10);
        cache.put(20);
        cache.put(30);
        cache.put(40);
        cache.printState();

        // Hit 10 and 20 so their ref bits are set
        std::cout << "\nget(10) = " << cache.get(10) << "\n";
        std::cout << "get(20) = " << cache.get(20) << "\n";
        cache.printState();

        // Insert 50 — cache is full; 30 has ref=0 so it gets evicted first
        std::cout << "\nput(50) — triggers eviction\n";
        cache.put(50);
        cache.printState();

        try {
            cache.get(30);
        } catch (const std::out_of_range& e) {
            std::cout << "\nExpected miss: " << e.what() << "\n";
        }
    }

    std::cout << "\n=== ClockSweep<std::string> (capacity 3) ===\n";
    {
        ClockSweep<std::string> cache(3, 60);

        cache.put("alice");
        cache.put("bob");
        cache.put("carol");
        cache.printState();

        cache.get("alice");             // refresh alice
        cache.put("dave");              // bob or carol evicted
        cache.printState();
    }

    return 0;
}