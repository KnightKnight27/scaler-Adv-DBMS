// Clock Sweep Buffer Pool  — Lab 3
// Tanishq Singh | 24BCS10303
//
// I went with a variant-based key so the same pool works with int, string,
// and a simple Page struct without needing two separate instantiations.
// The eviction side is straight clock sweep — each frame has a usage counter
// that gets bumped on access (capped at 4) and decremented by the hand
// on every pass. Frame is only eligible when usage==0 AND pin==0.
//
// Build:
//   cmake -S . -B build && cmake --build build
//   ./build/clock_sweep
//
// Or just:
//   g++ -std=c++17 -pthread main.cpp -o clock_sweep && ./clock_sweep

#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <variant>
#include <optional>
#include <mutex>
#include <thread>
#include <chrono>
#include <stdexcept>
#include <sstream>

// ---- Page type (for the "works with pages" requirement) ----
struct Page {
    int   page_id  = -1;
    char  data[64] = {};   // simplified page content

    Page() = default;
    Page(int id, const std::string& content) : page_id(id) {
        // copy at most 63 chars
        std::size_t n = content.size() < 63 ? content.size() : 63;
        content.copy(data, n);
        data[n] = '\0';
    }
};

// ---- Key type: int or string ----
using Key = std::variant<int, std::string>;

// helper: turn any key into a printable string
static std::string keyStr(const Key& k) {
    if (std::holds_alternative<int>(k))
        return std::to_string(std::get<int>(k));
    return std::get<std::string>(k);
}

// ---- Value type: int, string, or Page ----
using Value = std::variant<int, std::string, Page>;

static std::string valStr(const Value& v) {
    if (std::holds_alternative<int>(v))         return std::to_string(std::get<int>(v));
    if (std::holds_alternative<std::string>(v)) return std::get<std::string>(v);
    // Page
    const Page& p = std::get<Page>(v);
    return std::string("Page(id=") + std::to_string(p.page_id) + ", data=\"" + p.data + "\")";
}

// ---- Custom hash for variant key ----
struct KeyHash {
    std::size_t operator()(const Key& k) const {
        if (std::holds_alternative<int>(k))
            return std::hash<int>{}(std::get<int>(k));
        return std::hash<std::string>{}(std::get<std::string>(k));
    }
};

// ======================================================================
// ClockSweepPool
// ======================================================================
class ClockSweepPool {
public:
    static constexpr int MAX_USAGE = 4;   // usage cap (similar to PG's BM_MAX_USAGE_COUNT=5)

    explicit ClockSweepPool(std::size_t cap) : cap_(cap), hand_(0) {
        if (cap == 0) throw std::invalid_argument("pool capacity must be > 0");
        frames_.reserve(cap);
    }

    // putKey — insert or update a key-value pair.
    // If already in pool: bump usage, update value, re-pin.
    // If pool not full: grab an empty slot.
    // Otherwise: run clock sweep to find a victim.
    // New / re-inserted entries start pinned (pin=1). Call unpinKey when done.
    void putKey(const Key& k, const Value& v) {
        std::lock_guard<std::mutex> lk(mu_);

        auto it = idx_.find(k);
        if (it != idx_.end()) {
            Frame& f = frames_[it->second];
            bumpUsage(f);
            f.pin++;
            f.val = v;
            log("PUT(update) k=" + keyStr(k) + " usage=" + std::to_string(f.usage) + " pin=" + std::to_string(f.pin));
            return;
        }

        if (frames_.size() < cap_) {
            std::size_t slot = frames_.size();
            frames_.push_back({k, v, 1, 1});
            idx_[k] = slot;
            log("PUT(new)    k=" + keyStr(k) + " -> slot " + std::to_string(slot));
            return;
        }

        std::size_t victim = sweep();
        Frame& f = frames_[victim];
        log("EVICT       k=" + keyStr(f.key) + " from slot " + std::to_string(victim));
        idx_.erase(f.key);

        f.key   = k;
        f.val   = v;
        f.usage = 1;
        f.pin   = 1;
        idx_[k] = victim;
        log("PUT(evict)  k=" + keyStr(k) + " -> slot " + std::to_string(victim));
    }

    // getKey — look up a key, return value if present.
    // On hit: bumps usage and increments pin. std::nullopt on miss.
    std::optional<Value> getKey(const Key& k) {
        std::lock_guard<std::mutex> lk(mu_);

        auto it = idx_.find(k);
        if (it == idx_.end()) {
            misses_++;
            log("GET(miss)   k=" + keyStr(k));
            return std::nullopt;
        }
        Frame& f = frames_[it->second];
        bumpUsage(f);
        f.pin++;
        hits_++;
        log("GET(hit)    k=" + keyStr(k) + " usage=" + std::to_string(f.usage) + " pin=" + std::to_string(f.pin));
        return f.val;
    }

    // unpinKey — release a pin held by the caller.
    // A frame is only evictable when pin reaches 0.
    void unpinKey(const Key& k) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = idx_.find(k);
        if (it == idx_.end()) return;
        Frame& f = frames_[it->second];
        if (f.pin > 0) {
            f.pin--;
            log("UNPIN       k=" + keyStr(k) + " pin=" + std::to_string(f.pin));
        }
    }

    void printPool(const std::string& label = "") const {
        std::lock_guard<std::mutex> lk(mu_);
        std::cout << "\n-- pool state [" << label << "] --"
                  << "  cap=" << cap_ << "  hand=" << hand_
                  << "  hits=" << hits_ << "  misses=" << misses_ << "\n";
        for (std::size_t i = 0; i < frames_.size(); i++) {
            const Frame& f = frames_[i];
            std::cout << "  slot[" << i << "]"
                      << (i == hand_ ? " <hand>" : "       ")
                      << "  k=" << keyStr(f.key)
                      << "  v=" << valStr(f.val)
                      << "  usage=" << f.usage
                      << "  pin="   << f.pin
                      << "\n";
        }
        std::cout << std::flush;
    }

    std::uint64_t hits()   const { std::lock_guard<std::mutex> lk(mu_); return hits_; }
    std::uint64_t misses() const { std::lock_guard<std::mutex> lk(mu_); return misses_; }
    std::size_t   size()   const { std::lock_guard<std::mutex> lk(mu_); return frames_.size(); }

private:
    struct Frame {
        Key   key;
        Value val;
        int   usage = 0;
        int   pin   = 0;
    };

    // Clock sweep: walk the hand, decrement usage on warm frames, skip pinned frames.
    // Returns index of chosen victim. Throws if every frame is pinned.
    std::size_t sweep() {
        const std::size_t n = frames_.size();
        // worst case: MAX_USAGE full revolutions to drain all usage counts, +1 to land on victim
        const std::size_t limit = (std::size_t)(MAX_USAGE + 1) * n + 1;

        for (std::size_t step = 0; step < limit; step++) {
            std::size_t cur = hand_;
            hand_ = (hand_ + 1) % n;
            Frame& f = frames_[cur];

            if (f.pin > 0)    continue;          // still held by someone
            if (f.usage > 0) { f.usage--; continue; }  // second chance

            return cur;   // cold + unpinned = victim
        }
        throw std::runtime_error("all frames are pinned, cannot evict");
    }

    void bumpUsage(Frame& f) {
        if (f.usage < MAX_USAGE) f.usage++;
    }

    void log(const std::string& msg) const {
        std::cout << "  [pool] " << msg << "\n";
    }

    const std::size_t cap_;
    std::vector<Frame> frames_;
    std::unordered_map<Key, std::size_t, KeyHash> idx_;
    std::size_t   hand_;
    std::uint64_t hits_   = 0;
    std::uint64_t misses_ = 0;
    mutable std::mutex mu_;
};

// ---- helpers for cleaner demo output ----
static void section(const std::string& s) {
    std::cout << "\n========== " << s << " ==========\n";
}

int main() {

    // --- Part 1: integer keys + string values ---
    section("1) fill pool with int keys, string values");
    ClockSweepPool pool(4);

    pool.putKey(1, std::string("row-1"));
    pool.putKey(2, std::string("row-2"));
    pool.putKey(3, std::string("row-3"));
    pool.putKey(4, std::string("row-4"));
    pool.printPool("after fill");

    // unpin everything so sweep can evict freely
    for (int k : {1, 2, 3, 4}) pool.unpinKey(k);
    pool.printPool("after unpin");

    // --- Part 2: heat up some entries ---
    section("2) access key=1 and key=2 multiple times to raise usage");
    pool.getKey(1); pool.unpinKey(1);
    pool.getKey(1); pool.unpinKey(1);
    pool.getKey(1); pool.unpinKey(1);   // usage at cap
    pool.getKey(2); pool.unpinKey(2);
    pool.printPool("heated up 1 and 2");

    // --- Part 3: trigger eviction ---
    section("3) insert key=5 -> should evict the coldest unpinned frame");
    // key=3 has usage=1 (from load), key=4 same. Hand starts at 0.
    // Hand will decrement 1's usage, then 2's, eventually evict 3 or 4.
    pool.putKey(5, std::string("row-5"));
    pool.unpinKey(5);
    pool.printPool("after insert 5");

    // --- Part 4: string keys ---
    section("4) string keys");
    ClockSweepPool spool(3);
    spool.putKey(std::string("user:alice"),   std::string("alice_data"));
    spool.putKey(std::string("user:bob"),     std::string("bob_data"));
    spool.putKey(std::string("user:charlie"), std::string("charlie_data"));
    for (auto& k : {std::string("user:alice"), std::string("user:bob"), std::string("user:charlie")})
        spool.unpinKey(k);

    spool.getKey(std::string("user:alice")); spool.unpinKey(std::string("user:alice"));
    spool.printPool("string keys, alice is warm");

    // inserting a 4th key forces eviction — alice is warm, so bob or charlie goes
    spool.putKey(std::string("user:dave"), std::string("dave_data"));
    spool.unpinKey(std::string("user:dave"));
    spool.printPool("after inserting dave");

    // --- Part 5: Page values ---
    section("5) Page values (pool works with page structs too)");
    ClockSweepPool ppool(3);
    ppool.putKey(101, Value(Page(101, "SELECT * FROM users")));
    ppool.putKey(102, Value(Page(102, "INSERT INTO orders")));
    ppool.putKey(103, Value(Page(103, "UPDATE stock SET qty")));
    ppool.unpinKey(101); ppool.unpinKey(102); ppool.unpinKey(103);
    ppool.printPool("page pool filled");

    // warm up page 101
    ppool.getKey(101); ppool.unpinKey(101);
    ppool.getKey(101); ppool.unpinKey(101);

    // load page 104 -> triggers sweep, 102 or 103 goes
    ppool.putKey(104, Value(Page(104, "DELETE FROM logs")));
    ppool.unpinKey(104);
    ppool.printPool("after loading page 104");

    // fetch a value and read the page
    auto res = ppool.getKey(101);
    if (res) {
        std::cout << "\n  fetched: " << valStr(*res) << "\n";
        ppool.unpinKey(101);
    }

    // --- Part 6: pin protection ---
    section("6) pin protection — pinned frame must not be evicted");
    ClockSweepPool pp(3);
    pp.putKey(10, std::string("A"));
    pp.putKey(20, std::string("B"));
    pp.putKey(30, std::string("C"));
    // unpin 20 and 30 but keep 10 pinned
    pp.unpinKey(20); pp.unpinKey(30);
    pp.printPool("before insert 40, key=10 is pinned");
    pp.putKey(40, std::string("D"));  // must not touch slot of key=10
    pp.unpinKey(40);
    pp.printPool("after insert 40");

    // --- Part 7: cache miss ---
    section("7) cache miss on key that was never inserted");
    auto miss = pool.getKey(999);
    if (!miss) std::cout << "  getKey(999) -> not in pool (expected)\n";

    // --- Part 8: stats ---
    section("8) stats");
    std::cout << "  pool  hits=" << pool.hits() << "  misses=" << pool.misses() << "\n";
    std::cout << "  spool hits=" << spool.hits() << "  misses=" << spool.misses() << "\n";
    std::cout << "  ppool hits=" << ppool.hits() << "  misses=" << ppool.misses() << "\n";

    // --- Part 9: concurrency test ---
    section("9) concurrency — 4 threads hammering a shared pool");
    ClockSweepPool shared(6);
    auto worker = [&shared](int base) {
        for (int i = 0; i < 150; i++) {
            Key k = base + (i % 12);
            shared.putKey(k, std::string("v") + std::to_string(std::get<int>(k)));
            shared.unpinKey(k);
            auto v = shared.getKey(k);
            if (v) shared.unpinKey(k);
        }
    };

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; t++) threads.emplace_back(worker, t * 100);
    for (auto& t : threads) t.join();

    std::cout << "  done. hits=" << shared.hits() << "  misses=" << shared.misses() << "\n";

    return 0;
}
