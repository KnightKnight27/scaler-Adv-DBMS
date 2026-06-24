// Clock Sweep Buffer Replacement Algorithm
// ADBMS Lab 3 — 24BCS10288 Vibhuti Bhatnagar
//
// Implementation modelled on PostgreSQL's buffer manager
// (src/backend/storage/buffer/freelist.c). Each frame in the pool carries
// a usage_count (0..MAX_USAGE_COUNT) and a pin count. A single "clock hand"
// rotates through the frames; on every visit it decrements usage_count.
// A frame is evictable only when it is unpinned AND its usage_count is 0.
//
// Build:   cmake -S . -B build && cmake --build build
// Run:     ./build/db_engine

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace adbms {

// PostgreSQL caps usage_count at BM_MAX_USAGE_COUNT = 5 so a single hot page
// cannot dominate the pool forever. We keep the same cap.
constexpr int kMaxUsageCount = 5;

template <typename Key, typename Value>
class ClockSweepBufferPool {
public:
    explicit ClockSweepBufferPool(std::size_t capacity)
        : capacity_(capacity), hand_(0) {
        if (capacity == 0) {
            throw std::invalid_argument("buffer pool capacity must be > 0");
        }
        frames_.reserve(capacity);
    }

    // Read a page. Increments usage_count (capped) and pins the frame
    // for the duration of the caller's reference. Returns std::nullopt on miss.
    std::optional<Value> get(const Key& key) {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = index_.find(key);
        if (it == index_.end()) {
            ++misses_;
            log("MISS  key=" + toStr(key));
            return std::nullopt;
        }
        Frame& f = frames_[it->second];
        bumpUsage(f);
        ++f.pin_count;
        ++hits_;
        log("HIT   key=" + toStr(key) +
            "  usage=" + std::to_string(f.usage_count) +
            "  pin=" + std::to_string(f.pin_count));
        return f.value;
    }

    // Insert / load a page. If the key is already cached, treat it as a hit.
    // If the pool has free slots, fill one. Otherwise run the clock sweep
    // to evict a victim. New frames start with usage_count = 1 and pin_count = 1.
    void put(const Key& key, const Value& value) {
        std::lock_guard<std::mutex> lock(mu_);

        auto it = index_.find(key);
        if (it != index_.end()) {
            Frame& f = frames_[it->second];
            bumpUsage(f);
            ++f.pin_count;
            f.value = value;
            log("REPIN key=" + toStr(key) +
                "  usage=" + std::to_string(f.usage_count) +
                "  pin=" + std::to_string(f.pin_count));
            return;
        }

        if (frames_.size() < capacity_) {
            std::size_t slot = frames_.size();
            frames_.push_back(Frame{key, value, /*usage=*/1, /*pin=*/1, /*valid=*/true});
            index_[key] = slot;
            log("LOAD  key=" + toStr(key) + " into slot " + std::to_string(slot));
            return;
        }

        std::size_t victim = pickVictim();   // runs the clock sweep
        Frame& v = frames_[victim];
        log("EVICT key=" + toStr(v.key) + " from slot " + std::to_string(victim));
        index_.erase(v.key);
        v.key = key;
        v.value = value;
        v.usage_count = 1;
        v.pin_count = 1;
        v.valid = true;
        index_[key] = victim;
        log("LOAD  key=" + toStr(key) + " into slot " + std::to_string(victim));
    }

    // Release a previously pinned frame. Eviction can only reclaim pages whose
    // pin_count has fallen back to 0.
    void unpin(const Key& key) {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = index_.find(key);
        if (it == index_.end()) return;
        Frame& f = frames_[it->second];
        if (f.pin_count > 0) {
            --f.pin_count;
            log("UNPIN key=" + toStr(key) +
                "  pin=" + std::to_string(f.pin_count));
        }
    }

    // Debug: print the whole pool in clock-hand order.
    void dump(const std::string& label = "") const {
        std::lock_guard<std::mutex> lock(mu_);
        std::cout << "\n--- pool state " << label << " ---\n";
        std::cout << "  capacity=" << capacity_
                  << "  hand=" << hand_
                  << "  hits=" << hits_
                  << "  misses=" << misses_ << "\n";
        for (std::size_t i = 0; i < frames_.size(); ++i) {
            const Frame& f = frames_[i];
            std::cout << "  [" << std::setw(2) << i << "]"
                      << (i == hand_ ? " <-hand " : "        ")
                      << " key=" << std::setw(4) << toStr(f.key)
                      << "  usage=" << f.usage_count
                      << "  pin=" << f.pin_count
                      << (f.valid ? "" : "  (empty)")
                      << "\n";
        }
        std::cout << std::flush;
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mu_);
        return frames_.size();
    }
    std::uint64_t hits() const   { std::lock_guard<std::mutex> lock(mu_); return hits_; }
    std::uint64_t misses() const { std::lock_guard<std::mutex> lock(mu_); return misses_; }

private:
    struct Frame {
        Key   key{};
        Value value{};
        int   usage_count = 0;
        int   pin_count   = 0;
        bool  valid       = false;
    };

    // The actual clock sweep. Walks the hand forward; on each visit, if
    // usage_count > 0 it decrements and moves on (the page got a "second
    // chance"). When it finds an unpinned frame whose usage_count is already
    // 0, that frame is the victim. Pinned frames are skipped entirely — a
    // page that all-the-callers are holding open cannot be reclaimed.
    std::size_t pickVictim() {
        const std::size_t n = frames_.size();
        // safety bound: at most kMaxUsageCount + 1 full revolutions are
        // enough to drive every unpinned frame's usage_count to 0.
        const std::size_t safety = static_cast<std::size_t>(kMaxUsageCount + 1) * n + 1;

        for (std::size_t step = 0; step < safety; ++step) {
            Frame& f = frames_[hand_];
            std::size_t cur = hand_;
            hand_ = (hand_ + 1) % n;

            if (f.pin_count > 0) {
                continue;                       // pinned — never a victim
            }
            if (f.usage_count > 0) {
                --f.usage_count;                // second chance, then move on
                continue;
            }
            return cur;                          // unpinned + usage 0 -> evict
        }
        throw std::runtime_error("no evictable frame: every page is pinned");
    }

    void bumpUsage(Frame& f) {
        if (f.usage_count < kMaxUsageCount) ++f.usage_count;
    }

    template <typename T>
    static std::string toStr(const T& v) {
        if constexpr (std::is_same_v<T, std::string>) return v;
        else return std::to_string(v);
    }

    void log(const std::string& msg) const {
        std::cout << "  [pool] " << msg << "\n";
    }

    const std::size_t capacity_;
    std::vector<Frame> frames_;
    std::unordered_map<Key, std::size_t> index_;
    std::size_t hand_;
    std::uint64_t hits_   = 0;
    std::uint64_t misses_ = 0;
    mutable std::mutex mu_;
};

}  // namespace adbms

// ---------------------------------------------------------------------------
// Demonstration: walk through the scenarios that exercise each branch of the
// algorithm. Each scenario prints the pool state so the reader can follow
// the clock hand.
// ---------------------------------------------------------------------------

static void heading(const std::string& s) {
    std::cout << "\n================ " << s << " ================\n";
}

int main() {
    using Pool = adbms::ClockSweepBufferPool<int, std::string>;
    Pool pool(4);                  // four buffer frames

    heading("1) Fill the pool");
    pool.put(10, "page-10");
    pool.put(20, "page-20");
    pool.put(30, "page-30");
    pool.put(40, "page-40");
    pool.dump("after initial fill");

    // Release pins so subsequent inserts can evict. In PG the bgwriter /
    // backends drop pins when they finish reading the page.
    for (int k : {10, 20, 30, 40}) pool.unpin(k);
    pool.dump("after unpin all");

    heading("2) Re-reference some pages (boost usage_count)");
    pool.get(10);  pool.unpin(10);
    pool.get(10);  pool.unpin(10);   // 10 becomes very "hot"
    pool.get(20);  pool.unpin(20);
    pool.dump("after re-references");

    heading("3) Insert 50 -> must trigger a clock sweep");
    // Expected: hand passes 10 (usage>0 -> dec), 20 (usage>0 -> dec),
    // 30 (usage 0 -> EVICT). Page 30 is the victim because it was the
    // coldest unpinned frame.
    pool.put(50, "page-50");
    pool.unpin(50);
    pool.dump("after insert 50");

    heading("4) Insert 60 -> another sweep, still skipping hot 10");
    pool.put(60, "page-60");
    pool.unpin(60);
    pool.dump("after insert 60");

    heading("5) Pin page 10 and insert 70 -> 10 must NOT be evicted");
    pool.get(10);                     // pin it (don't unpin yet)
    pool.put(70, "page-70");
    pool.unpin(70);
    pool.unpin(10);
    pool.dump("after insert 70 (10 stayed pinned)");

    heading("6) Stats");
    std::cout << "  total hits   = " << pool.hits() << "\n";
    std::cout << "  total misses = " << pool.misses() << "\n";
    std::cout << "  pool size    = " << pool.size() << "\n";

    heading("7) Concurrency smoke test");
    // A handful of threads bashing get/put to confirm no data races. With
    // ThreadSanitizer (-fsanitize=thread) this run is clean.
    Pool shared(8);
    auto worker = [&shared](int base) {
        for (int i = 0; i < 200; ++i) {
            int k = base + (i % 16);
            shared.put(k, "v-" + std::to_string(k));
            shared.unpin(k);
            auto v = shared.get(k);
            if (v) shared.unpin(k);
        }
    };
    std::vector<std::thread> ts;
    for (int t = 0; t < 4; ++t) ts.emplace_back(worker, t * 100);
    for (auto& th : ts) th.join();
    std::cout << "  concurrency test done."
              << "  hits=" << shared.hits()
              << "  misses=" << shared.misses() << "\n";

    return 0;
}
