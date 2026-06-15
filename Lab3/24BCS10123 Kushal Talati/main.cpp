// Lab 3 — Clock-Sweep Buffer Pool driver
// 24BCS10123  Kushal Talati
//
// Drives the BufferPool<PageId, Page> from buffer_pool.hpp through a
// scripted access trace (sequential scan, working-set, single hotspot,
// dirty flush) and then a multi-threaded contention test. Every state
// transition is dumped so the reader can watch the clock hand advance.

#include "buffer_pool.hpp"

#include <atomic>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {

// "Page" stand-in for the demo — a small struct carrying enough state
// to show that load-from-loader and write-back-via-writer actually do
// something. In a real system this would be a 4 KB / 8 KB byte buffer.
struct DemoPage {
    int  id      = -1;
    int  payload = 0;
    int  version = 0;
};
using Pool = kt::BufferPool<int, DemoPage>;

void banner(const std::string& s) {
    std::cout << "\n##### " << s << " #####\n";
}

// A loader that fakes "reading a page from disk". The version counter
// increments every time it runs, so we can see whether a frame holds a
// freshly loaded page or an older cached copy.
struct DiskStub {
    std::atomic<int> reads{0};
    DemoPage operator()(const int& id) {
        int v = reads.fetch_add(1) + 1;
        return DemoPage{id, id * 100, v};
    }
};

// A writer that counts how many pages get flushed.
struct DiskWriter {
    std::atomic<int> writes{0};
    void operator()(const int& id, const DemoPage& page) {
        (void)id; (void)page;
        writes.fetch_add(1);
    }
};

}  // namespace

int main() {
    DiskStub   disk;
    DiskWriter pen;
    Pool pool(/*capacity=*/4);

    // ---------------------------------------------------------------
    banner("A) Cold fill: pages 1..4 pulled in by acquire+release");
    // ---------------------------------------------------------------
    auto cb_load = [&](const int& id) { return disk(id); };
    auto cb_write = [&](const int& id, const DemoPage& p) { pen(id, p); };

    for (int id : {1, 2, 3, 4}) {
        pool.acquire(id, cb_load);
        pool.release(id);
    }
    pool.render(std::cout, "after cold fill");

    // ---------------------------------------------------------------
    banner("B) Heat up page 1 (re-acquire twice) so it survives evictions");
    // ---------------------------------------------------------------
    pool.acquire(1, cb_load); pool.release(1);
    pool.acquire(1, cb_load); pool.release(1);   // ref_bits should now be 3 on slot[0]
    pool.acquire(2, cb_load); pool.release(2);   // 2 bumped to 2
    pool.render(std::cout, "after heat-up of 1 and 2");

    // ---------------------------------------------------------------
    banner("C) Acquire page 5 -> first eviction; hand must skip hot 1");
    // ---------------------------------------------------------------
    pool.acquire(5, cb_load); pool.release(5);
    pool.render(std::cout, "after acquire(5)");

    // ---------------------------------------------------------------
    banner("D) Sequential scan over IDs 100..107 -> rapid evictions");
    // ---------------------------------------------------------------
    for (int id = 100; id < 108; ++id) {
        pool.acquire(id, cb_load);
        pool.release(id);
    }
    pool.render(std::cout, "after sequential scan");
    std::cout << "  disk reads so far = " << disk.reads.load() << "\n";

    // ---------------------------------------------------------------
    banner("E) Dirty + flush: mark some pages dirty and flush them out");
    // ---------------------------------------------------------------
    // Re-acquire whatever's currently in the pool and mark dirty.
    auto m = pool.metrics();
    (void)m;
    for (int id = 100; id < 108; ++id) {
        // Some of these will hit cache, some will miss; either way the
        // post-condition is they're pinned, dirty, then released.
        pool.acquire(id, cb_load);
        pool.release(id, /*dirty=*/true);
    }
    std::size_t flushed = pool.flush_all(cb_write);
    std::cout << "  flushed " << flushed << " dirty page(s); "
              << "disk writes = " << pen.writes.load() << "\n";
    pool.render(std::cout, "after flush");

    // ---------------------------------------------------------------
    banner("F) Pinned-page protection: an unreleased pin must survive a sweep");
    // ---------------------------------------------------------------
    pool.acquire(900, cb_load);                      // pin 900 — do NOT release
    for (int id = 200; id < 210; ++id) {
        pool.acquire(id, cb_load);
        pool.release(id);
    }
    // 900 must still be findable in the cache; nothing has been able to
    // evict it because pin_count was > 0 the entire time.
    auto still_there = pool.peek(900);
    std::cout << "  peek(900) = "
              << (still_there ? "found (survived)" : "EVICTED (bug!)") << "\n";
    pool.release(900);                                // unpin and continue
    pool.render(std::cout, "after pinned-900 sweep stress");

    // ---------------------------------------------------------------
    banner("G) Final metrics");
    // ---------------------------------------------------------------
    auto stats = pool.metrics();
    std::cout << "  hits        = " << stats.hits << "\n"
              << "  misses      = " << stats.misses << "\n"
              << "  evictions   = " << stats.evictions << "\n"
              << "  hand visits = " << stats.hand_visits << "\n"
              << "  hand rounds = " << stats.hand_rounds << "\n"
              << "  hit ratio   = "
              << (stats.hits + stats.misses == 0 ? 0.0
                 : (double)stats.hits / (stats.hits + stats.misses))
              << "\n";

    // ---------------------------------------------------------------
    banner("H) Multi-threaded contention smoke test");
    // ---------------------------------------------------------------
    Pool shared(/*capacity=*/8);
    DiskStub shared_disk;
    auto shared_load = [&](const int& id) { return shared_disk(id); };

    auto worker = [&](int seed) {
        std::mt19937 rng(static_cast<unsigned>(seed));
        std::uniform_int_distribution<int> pick(0, 31);
        for (int i = 0; i < 400; ++i) {
            int id = pick(rng);
            shared.acquire(id, shared_load);
            shared.release(id, /*dirty=*/(i % 7 == 0));
        }
    };
    std::vector<std::thread> th;
    for (int t = 0; t < 6; ++t) th.emplace_back(worker, t + 1);
    for (auto& x : th) x.join();

    auto sshared = shared.metrics();
    std::cout << "  shared.hits=" << sshared.hits
              << "  shared.miss=" << sshared.misses
              << "  shared.evict=" << sshared.evictions
              << "  hand_rounds=" << sshared.hand_rounds
              << "\n  (numbers vary per run; no crash / no deadlock is the point)\n";

    return 0;
}
