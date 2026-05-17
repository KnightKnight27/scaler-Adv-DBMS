#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace adbms {

constexpr int kMaxUsageCount = 5;
constexpr auto kSweepInterval = std::chrono::milliseconds(200);

template <typename T>
class ClockSweep {
public:
    explicit ClockSweep(std::size_t maxCacheSize)
        : maxCacheSize_(maxCacheSize), hand_(0), stop_(false) {
        if (maxCacheSize == 0) {
            throw std::invalid_argument("cache size must be > 0");
        }
        frames_.reserve(maxCacheSize);
        bgClockThread_ = std::thread(&ClockSweep::evictionClockThread, this);
    }

    ~ClockSweep() {
        {
            std::lock_guard<std::mutex> lock(mu_);
            stop_ = true;
        }
        cv_.notify_all();
        if (bgClockThread_.joinable()) bgClockThread_.join();
    }

    ClockSweep(const ClockSweep&)            = delete;
    ClockSweep& operator=(const ClockSweep&) = delete;

    std::optional<T> get(const T& key) {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = index_.find(key);
        if (it == index_.end()) {
            ++misses_;
            log("MISS  key=" + toStr(key));
            return std::nullopt;
        }
        Frame& f = frames_[it->second];
        bumpUsage(f);
        ++hits_;
        log("HIT   key=" + toStr(key) +
            "  usage=" + std::to_string(f.usage_count));
        return f.key;
    }

    void put(const T& key) {
        std::lock_guard<std::mutex> lock(mu_);

        auto it = index_.find(key);
        if (it != index_.end()) {
            Frame& f = frames_[it->second];
            bumpUsage(f);
            log("TOUCH key=" + toStr(key) +
                "  usage=" + std::to_string(f.usage_count));
            return;
        }

        if (frames_.size() < maxCacheSize_) {
            std::size_t slot = frames_.size();
            frames_.push_back(Frame{key, 1, true});
            index_[key] = slot;
            log("LOAD  key=" + toStr(key) + " into slot " + std::to_string(slot));
            return;
        }

        std::size_t victim = pickVictim();
        Frame& v = frames_[victim];
        log("EVICT key=" + toStr(v.key) + " from slot " + std::to_string(victim));
        index_.erase(v.key);
        v.key = key;
        v.usage_count = 1;
        v.valid = true;
        index_[key] = victim;
        log("LOAD  key=" + toStr(key) + " into slot " + std::to_string(victim));
    }

    void dump(const std::string& label = "") const {
        std::lock_guard<std::mutex> lock(mu_);
        std::cout << "\n--- cache " << label << " ---\n";
        std::cout << "  capacity=" << maxCacheSize_
                  << "  hand=" << hand_
                  << "  hits=" << hits_
                  << "  misses=" << misses_ << "\n";
        for (std::size_t i = 0; i < frames_.size(); ++i) {
            const Frame& f = frames_[i];
            std::cout << "  [" << std::setw(2) << i << "]"
                      << (i == hand_ ? " <-hand " : "        ")
                      << " key=" << std::setw(8) << toStr(f.key)
                      << "  usage=" << f.usage_count
                      << (f.valid ? "" : "  (empty)") << "\n";
        }
        std::cout << std::flush;
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mu_);
        return frames_.size();
    }
    std::uint64_t hits() const   { std::lock_guard<std::mutex> lock(mu_); return hits_;   }
    std::uint64_t misses() const { std::lock_guard<std::mutex> lock(mu_); return misses_; }

private:
    struct Frame {
        T    key{};
        int  usage_count = 0;
        bool valid       = false;
    };

    std::size_t pickVictim() {
        const std::size_t n = frames_.size();
        const std::size_t safety = static_cast<std::size_t>(kMaxUsageCount + 1) * n + 1;

        for (std::size_t step = 0; step < safety; ++step) {
            Frame& f = frames_[hand_];
            std::size_t cur = hand_;
            hand_ = (hand_ + 1) % n;
            if (f.usage_count > 0) {
                --f.usage_count;
                continue;
            }
            return cur;
        }
        throw std::runtime_error("clock sweep failed to find a victim");
    }

    void evictionClockThread() {
        std::unique_lock<std::mutex> lock(mu_);
        while (!stop_) {
            if (cv_.wait_for(lock, kSweepInterval, [this] { return stop_.load(); })) {
                return;
            }
            if (frames_.empty()) continue;
            Frame& f = frames_[hand_];
            if (f.usage_count > 0) --f.usage_count;
            hand_ = (hand_ + 1) % frames_.size();
        }
    }

    void bumpUsage(Frame& f) {
        if (f.usage_count < kMaxUsageCount) ++f.usage_count;
    }

    template <typename U>
    static std::string toStr(const U& v) {
        if constexpr (std::is_same_v<U, std::string>) return v;
        else if constexpr (std::is_arithmetic_v<U>)   return std::to_string(v);
        else return std::string("<key>");
    }

    void log(const std::string& msg) const {
        std::cout << "  [cache] " << msg << "\n";
    }

    const std::size_t           maxCacheSize_;
    std::vector<Frame>          frames_;
    std::unordered_map<T, std::size_t> index_;
    std::size_t                 hand_;
    std::uint64_t               hits_   = 0;
    std::uint64_t               misses_ = 0;
    mutable std::mutex          mu_;
    std::condition_variable     cv_;
    std::atomic<bool>           stop_;
    std::thread                 bgClockThread_;
};

}

static void heading(const std::string& s) {
    std::cout << "\n================ " << s << " ================\n";
}

int main() {
    using IntCache = adbms::ClockSweep<int>;

    heading("1) Fill the cache (capacity = 4)");
    IntCache cache(4);
    cache.put(10);
    cache.put(20);
    cache.put(30);
    cache.put(40);
    cache.dump("after initial fill");

    heading("2) Re-touch some keys to grow usage_count");
    cache.put(10); cache.put(10);
    cache.put(20);
    cache.dump("after re-touches");

    heading("3) Insert 50 -> clock sweep picks the coldest frame");
    cache.put(50);
    cache.dump("after insert 50");

    heading("4) Insert 60 -> another eviction, hot keys keep surviving");
    cache.put(60);
    cache.dump("after insert 60");

    heading("5) Demonstrate the background thread decaying counts");
    cache.put(10); cache.put(10); cache.put(10);
    cache.dump("before sleep");
    std::this_thread::sleep_for(std::chrono::seconds(2));
    cache.dump("after 2s of background sweeps");

    heading("6) String cache (template re-use)");
    adbms::ClockSweep<std::string> sc(3);
    sc.put("alpha"); sc.put("beta"); sc.put("gamma");
    sc.get("alpha"); sc.get("alpha");
    sc.put("delta");
    sc.dump("string cache state");

    heading("7) Stats");
    std::cout << "  int cache    hits=" << cache.hits()
              << "  misses="           << cache.misses()
              << "  size="             << cache.size() << "\n";
    std::cout << "  string cache hits=" << sc.hits()
              << "  misses="            << sc.misses()
              << "  size="              << sc.size() << "\n";

    heading("8) Concurrency smoke test");
    IntCache shared(8);
    auto worker = [&shared](int base) {
        for (int i = 0; i < 200; ++i) {
            int k = base + (i % 16);
            shared.put(k);
            (void)shared.get(k);
        }
    };
    std::vector<std::thread> ts;
    for (int t = 0; t < 4; ++t) ts.emplace_back(worker, t * 100);
    for (auto& th : ts) th.join();
    std::cout << "  concurrency done.  hits=" << shared.hits()
              << "  misses=" << shared.misses() << "\n";

    return 0;
}
