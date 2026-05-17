#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

template <typename K>
class SecondChanceCache {
public:
    using Clock = std::chrono::steady_clock;
    using Duration = std::chrono::milliseconds;

    explicit SecondChanceCache(std::size_t capacity,
                               Duration agingPeriod = Duration(500))
        : frames_(capacity),
          capacity_(capacity),
          cursor_(0),
          agingPeriod_(agingPeriod),
          shutdown_(false) {
        if (capacity_ == 0) {
            throw std::invalid_argument("Cache capacity must be > 0");
        }
        ager_ = std::thread(&SecondChanceCache::ageLoop, this);
    }

    ~SecondChanceCache() {
        {
            std::lock_guard<std::mutex> lk(mu_);
            shutdown_ = true;
        }
        wake_.notify_all();
        if (ager_.joinable()) ager_.join();
    }

    SecondChanceCache(const SecondChanceCache&) = delete;
    SecondChanceCache& operator=(const SecondChanceCache&) = delete;

    void put(const K& key) {
        std::lock_guard<std::mutex> lk(mu_);

        if (auto it = lookup_.find(key); it != lookup_.end()) {
            frames_[it->second].used = true;
            return;
        }

        // Try to fill an empty frame first.
        for (std::size_t i = 0; i < capacity_; ++i) {
            if (!frames_[i].live) {
                place(i, key);
                return;
            }
        }

        // No empty slot — evict via clock sweep.
        const std::size_t victim = sweepForVictim();
        lookup_.erase(frames_[victim].key);
        place(victim, key);
        cursor_ = (victim + 1) % capacity_;
    }

    K get(const K& key) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = lookup_.find(key);
        if (it == lookup_.end()) return K{};
        frames_[it->second].used = true;
        return frames_[it->second].key;
    }

    bool contains(const K& key) {
        std::lock_guard<std::mutex> lk(mu_);
        return lookup_.find(key) != lookup_.end();
    }

    std::size_t size() {
        std::lock_guard<std::mutex> lk(mu_);
        return lookup_.size();
    }

    void dump(const std::string& label) {
        std::lock_guard<std::mutex> lk(mu_);
        std::cout << "\n[" << label << "] cursor=" << cursor_ << " -> ";
        for (std::size_t i = 0; i < capacity_; ++i) {
            const auto& f = frames_[i];
            if (f.live) {
                std::cout << f.key << "(ref=" << f.used << ") ";
            } else {
                std::cout << "empty ";
            }
        }
        std::cout << '\n';
    }

private:
    struct Frame {
        K key{};
        bool live = false;
        bool used = false;
    };

    void place(std::size_t idx, const K& key) {
        frames_[idx].key = key;
        frames_[idx].live = true;
        frames_[idx].used = true;
        lookup_[key] = idx;
    }

    std::size_t sweepForVictim() {
        while (true) {
            Frame& f = frames_[cursor_];
            if (!f.used) return cursor_;
            f.used = false;
            cursor_ = (cursor_ + 1) % capacity_;
        }
    }

    void ageLoop() {
        std::unique_lock<std::mutex> lk(mu_);
        while (!shutdown_) {
            if (wake_.wait_for(lk, agingPeriod_,
                               [this] { return shutdown_; })) {
                break;
            }
            for (auto& f : frames_) {
                if (f.live) f.used = false;
            }
        }
    }

    std::vector<Frame> frames_;
    std::unordered_map<K, std::size_t> lookup_;
    std::size_t capacity_;
    std::size_t cursor_;
    Duration agingPeriod_;

    std::mutex mu_;
    std::condition_variable wake_;
    bool shutdown_;
    std::thread ager_;
};

namespace demo {

void runIntegerScenario() {
    std::cout << "\n===== INTEGER CACHE =====\n";
    SecondChanceCache<int> cache(4, std::chrono::milliseconds(300));

    for (int v : {10, 20, 30, 40}) cache.put(v);
    cache.dump("Initial Fill");

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    cache.dump("After Aging");

    cache.get(20);
    cache.get(40);
    cache.dump("Touched 20 and 40");

    cache.put(50);
    cache.dump("Inserted 50");

    cache.put(60);
    cache.dump("Inserted 60");

    std::cout << "\n20 exists: " << cache.contains(20);
    std::cout << "\n10 exists: " << cache.contains(10);
    std::cout << "\nCurrent size: " << cache.size() << '\n';
}

void runStringScenario() {
    std::cout << "\n===== STRING CACHE =====\n";
    SecondChanceCache<std::string> cache(3, std::chrono::milliseconds(400));

    cache.put("apple");
    cache.put("banana");
    cache.put("orange");
    cache.dump("Initial");

    cache.get("banana");
    std::this_thread::sleep_for(std::chrono::milliseconds(600));

    cache.put("grape");
    cache.dump("After inserting grape");

    std::cout << "\nbanana exists: " << cache.contains("banana");
    std::cout << "\napple exists: " << cache.contains("apple") << '\n';
}

}  // namespace demo

int main() {
    demo::runIntegerScenario();
    demo::runStringScenario();
    std::cout << "\nProgram Finished\n";
    return 0;
}
