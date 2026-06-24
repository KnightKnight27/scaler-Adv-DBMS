#include <iostream>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <string>

template <typename T>
class ClockSweep {
private:
    struct Slot {
        T data{};
        bool referenced{false};
        bool used{false};
    };

    size_t capacity_;
    std::chrono::milliseconds interval_;
    std::vector<Slot> buffer_;
    std::unordered_map<T, size_t> lookup_;
    size_t ptr_{0};

    std::mutex mtx_;
    std::condition_variable cv_;
    bool kill_thread_{false};
    std::thread bg_thread_;

    // Scans for a victim slot. Assumes caller already locked mtx_.
    size_t runClockHand() {
        for (size_t i = 0; i < 2 * capacity_; ++i) {
            Slot& curr = buffer_[ptr_];
            
            if (curr.used && !curr.referenced) {
                return ptr_;
            }
            if (curr.used && curr.referenced) {
                curr.referenced = false; // Give second chance
            }
            ptr_ = (ptr_ + 1) % capacity_;
        }
        return ptr_; 
    }

    // Background function that resets referenced bits to false periodically
    void sweeperLoop() {
        std::unique_lock<std::mutex> lock(mtx_);
        while (!kill_thread_) {
            if (cv_.wait_for(lock, interval_, [this]() { return kill_thread_; })) {
                break; 
            }
            for (auto& slot : buffer_) {
                if (slot.used) {
                    slot.referenced = false;
                }
            }
        }
    }

public:
    explicit ClockSweep(size_t max_size, std::chrono::milliseconds sweep_ms = std::chrono::milliseconds(500))
        : capacity_(max_size), interval_(sweep_ms), buffer_(max_size) {
        bg_thread_ = std::thread(&ClockSweep::sweeperLoop, this);
    }

    ~ClockSweep() {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            kill_thread_ = true;
        }
        cv_.notify_all();
        if (bg_thread_.joinable()) {
            bg_thread_.join();
        }
    }

    // Delete copy constructors to prevent thread double-free issues
    ClockSweep(const ClockSweep&) = delete;
    ClockSweep& operator=(const ClockSweep&) = delete;

    T getKey(const T& key) {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = lookup_.find(key);
        if (it == lookup_.end()) {
            return T{}; // Not found
        }
        buffer_[it->second].referenced = true;
        return buffer_[it->second].data;
    }

    void putKey(const T& key) {
        std::lock_guard<std::mutex> lock(mtx_);

        // 1. If it already exists, just flip the reference bit back to true
        auto it = lookup_.find(key);
        if (it != lookup_.end()) {
            buffer_[it->second].referenced = true;
            return;
        }

        // 2. Look for an empty slot at the current pointer position
        for (size_t i = 0; i < capacity_; ++i) {
            if (!buffer_[ptr_].used) {
                buffer_[ptr_] = Slot{key, true, true};
                lookup_[key] = ptr_;
                ptr_ = (ptr_ + 1) % capacity_;
                return;
            }
            ptr_ = (ptr_ + 1) % capacity_;
        }

        // 3. Cache is completely full; find a victim to evict
        size_t victim_idx = runClockHand();
        
        lookup_.erase(buffer_[victim_idx].data);
        buffer_[victim_idx] = Slot{key, true, true};
        lookup_[key] = victim_idx;
        
        ptr_ = (victim_idx + 1) % capacity_;
    }

    bool contains(const T& key) {
        std::lock_guard<std::mutex> lock(mtx_);
        return lookup_.find(key) != lookup_.end();
    }

    size_t size() {
        std::lock_guard<std::mutex> lock(mtx_);
        return lookup_.size();
    }

    size_t capacity() const { 
        return capacity_; 
    }

    void debugPrint(const std::string& step_name) {
        std::lock_guard<std::mutex> lock(mtx_);
        std::cout << step_name << " (ptr=" << ptr_ << "): ";
        for (size_t i = 0; i < capacity_; ++i) {
            if (buffer_[i].used) {
                std::cout << "[" << buffer_[i].data << ":" << buffer_[i].referenced << "] ";
            } else {
                std::cout << "[ _ ] ";
            }
        }
        std::cout << "\n";
    }
};

void runDemoOne() {
    std::cout << "=== Test 1: Basic Integer Eviction ===\n";
    ClockSweep<int> c(3, std::chrono::milliseconds(300));

    c.putKey(10); c.debugPrint("Added 10");
    c.putKey(20); c.debugPrint("Added 20");
    c.putKey(30); c.debugPrint("Added 30");

    std::cout << "Sleeping 400ms to allow background thread to zero out the bits...\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    c.debugPrint("After sleep");

    c.getKey(20); c.debugPrint("Accessed 20");
    c.putKey(40); c.debugPrint("Added 40 (Evicts 10, keeps 20)");
    std::cout << "\n";
}

void runDemoTwo() {
    std::cout << "=== Test 2: Strings ===\n";
    ClockSweep<std::string> c(2);
    c.putKey("A");
    c.putKey("B");
    c.debugPrint("Filled");
    c.putKey("C");
    c.debugPrint("Added C (Evicts A)");
    std::cout << "\n";
}

void runDemoThree() {
    std::cout << "=== Test 3: Re-putting an existing key ===\n";
    ClockSweep<int> c(2, std::chrono::milliseconds(800));
    c.putKey(55);
    c.putKey(77);
    std::this_thread::sleep_for(std::chrono::milliseconds(900));
    c.debugPrint("Bits aged to 0");
    c.putKey(55); 
    c.debugPrint("Re-inserted 55 (Bit flipped back to 1)");
}

int main() {
    runDemoOne();
    runDemoTwo();
    runDemoThree();
    return 0;
}