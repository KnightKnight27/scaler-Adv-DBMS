#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

template <typename K, typename V>
class ClockSweep {
public:
    explicit ClockSweep(std::size_t capacity,
                        std::chrono::milliseconds sweepInterval = std::chrono::milliseconds(1500))
        : capacity_(capacity), hand_(0), slots_(capacity), sweepInterval_(sweepInterval), stop_(false) {
        sweeper_ = std::thread(&ClockSweep::sweepLoop, this);
    }

    ~ClockSweep() {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            stop_ = true;
        }
        cv_.notify_all();
        if (sweeper_.joinable()) sweeper_.join();
    }

    ClockSweep(const ClockSweep&) = delete;
    ClockSweep& operator=(const ClockSweep&) = delete;

    std::optional<V> get(const K& key) {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = index_.find(key);
        if (it == index_.end()) {
            ++misses_;
            return std::nullopt;
        }
        Slot& s = slots_[it->second];
        s.referenced = true;
        ++hits_;
        return s.value;
    }

    void put(const K& key, const V& value) {
        std::lock_guard<std::mutex> lock(mtx_);

        if (auto it = index_.find(key); it != index_.end()) {
            slots_[it->second].value = value;
            slots_[it->second].referenced = true;
            return;
        }

        std::size_t victim = chooseVictim();
        Slot& s = slots_[victim];
        if (s.occupied) {
            index_.erase(s.key);
            ++evictions_;
        }
        s.key = key;
        s.value = value;
        s.referenced = true;
        s.occupied = true;
        index_[key] = victim;
        hand_ = (victim + 1) % capacity_;
    }

    void dump(std::ostream& os) const {
        std::lock_guard<std::mutex> lock(mtx_);
        os << "[clock] hand=" << hand_ << " hits=" << hits_
           << " misses=" << misses_ << " evictions=" << evictions_ << "\n";
        for (std::size_t i = 0; i < slots_.size(); ++i) {
            const Slot& s = slots_[i];
            os << "  slot " << i << ": ";
            if (!s.occupied) {
                os << "<empty>\n";
            } else {
                os << "key=" << s.key << " val=" << s.value
                   << " ref=" << (s.referenced ? 1 : 0) << "\n";
            }
        }
    }

private:
    struct Slot {
        K key{};
        V value{};
        bool referenced{false};
        bool occupied{false};
    };

    std::size_t chooseVictim() {
        // First, prefer any free slot — start from the hand and wrap once.
        for (std::size_t i = 0; i < capacity_; ++i) {
            std::size_t idx = (hand_ + i) % capacity_;
            if (!slots_[idx].occupied) return idx;
        }
        // Otherwise do the second-chance sweep. Worst case: two full passes.
        for (std::size_t spins = 0; spins < 2 * capacity_; ++spins) {
            Slot& s = slots_[hand_];
            if (!s.referenced) return hand_;
            s.referenced = false;
            hand_ = (hand_ + 1) % capacity_;
        }
        return hand_;
    }

    void sweepLoop() {
        std::unique_lock<std::mutex> lock(mtx_);
        while (!stop_) {
            if (cv_.wait_for(lock, sweepInterval_, [this] { return stop_; })) return;
            // Background sweep nudges reference bits down so cold pages can be evicted
            // without waiting for a put() to walk the whole ring.
            for (auto& s : slots_) {
                if (s.occupied) s.referenced = false;
            }
        }
    }

    const std::size_t capacity_;
    std::size_t hand_;
    std::vector<Slot> slots_;
    std::unordered_map<K, std::size_t> index_;

    mutable std::mutex mtx_;
    std::condition_variable cv_;
    std::thread sweeper_;
    std::chrono::milliseconds sweepInterval_;
    bool stop_;

    std::size_t hits_{0};
    std::size_t misses_{0};
    std::size_t evictions_{0};
};

int main() {
    ClockSweep<int, std::string> cache(4, std::chrono::milliseconds(500));

    cache.put(1, "alpha");
    cache.put(2, "beta");
    cache.put(3, "gamma");
    cache.put(4, "delta");

    // Touch a couple of entries so their reference bits flip on.
    cache.get(1);
    cache.get(3);

    std::cout << "after initial fill + touches:\n";
    cache.dump(std::cout);

    // Insert a new key — sweep should evict the untouched slot (key 2 or 4).
    cache.put(5, "epsilon");
    std::cout << "\nafter inserting key 5 (one eviction expected):\n";
    cache.dump(std::cout);

    // After put(5) the sweep cleared every reference bit (slots 1,3 held ref=1)
    // and evicted the first slot it found with ref=0 on the second pass.
    for (int k : {1, 2, 3, 4, 5}) {
        if (auto v = cache.get(k)) {
            std::cout << "\nget(" << k << ") -> " << *v;
        } else {
            std::cout << "\nget(" << k << ") -> miss";
        }
    }
    std::cout << "\n";

    // Let the background sweep tick a few times, then prove cold keys get evicted on next put.
    std::this_thread::sleep_for(std::chrono::milliseconds(1600));
    cache.put(6, "zeta");
    cache.put(7, "eta");
    std::cout << "\nafter background sweep + two more puts:\n";
    cache.dump(std::cout);

    return 0;
}
