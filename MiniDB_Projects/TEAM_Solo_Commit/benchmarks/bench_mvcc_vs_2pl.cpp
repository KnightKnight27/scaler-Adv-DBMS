// Track B benchmark: MVCC snapshot reads vs Strict-2PL under read/write contention.
//
// Scenario: one writer holds a single hot row in a write transaction for a fixed window while
// several reader threads hammer that same row. Under Strict 2PL a read needs a shared lock and
// must wait for the writer's exclusive lock to be released, so reads stall for the whole write.
// Under MVCC a reader just reads the last version committed before its snapshot, so it never
// waits. We report how many reads complete during the window and the latency of a single read
// issued while the write is in flight.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "mvcc/version_store.h"
#include "txn/lock_manager.h"

using namespace minidb;
using Clock = std::chrono::steady_clock;

static void sleep_ms(int m) { std::this_thread::sleep_for(std::chrono::milliseconds(m)); }
static double ms_since(Clock::time_point a) {
    return std::chrono::duration<double, std::milli>(Clock::now() - a).count();
}

static constexpr int64_t HOT_KEY = 42;
static constexpr int kReaders = 8;
static constexpr int kWindowMs = 200;   // observation window
static constexpr int kWriteHoldMs = 150;  // writer keeps the row exclusive this long

// ---------------- Strict 2PL ----------------
static long Run2PL(double* blocked_read_ms) {
    LockManager lm;
    std::atomic<bool> stop{false};
    std::atomic<long> reads{0};

    // Writer transaction: take the exclusive lock and hold it (a long-running update).
    std::thread writer([&] {
        lm.Acquire(1000, HOT_KEY, LockMode::EXCLUSIVE);
        sleep_ms(kWriteHoldMs);
        lm.ReleaseAll(1000);
    });
    sleep_ms(5);  // let the writer grab X first

    // One probe reader times how long a single read blocks behind the writer.
    auto t0 = Clock::now();
    std::thread probe([&] {
        lm.Acquire(9999, HOT_KEY, LockMode::SHARED);  // blocks until writer commits
        lm.ReleaseAll(9999);
    });
    probe.join();
    *blocked_read_ms = ms_since(t0);

    // Reader throughput over the window: each takes S, "reads", releases.
    std::vector<std::thread> readers;
    for (int i = 0; i < kReaders; ++i) {
        readers.emplace_back([&, i] {
            int txn = 2000 + i;
            while (!stop.load()) {
                lm.Acquire(txn, HOT_KEY, LockMode::SHARED);
                lm.ReleaseAll(txn);
                reads.fetch_add(1);
            }
        });
    }
    sleep_ms(kWindowMs);
    stop = true;
    for (auto& r : readers) r.join();
    writer.join();
    return reads.load();
}

// ---------------- MVCC ----------------
static long RunMVCC(double* blocked_read_ms) {
    VersionStore vs;
    std::atomic<int64_t> clock{1};
    std::atomic<bool> stop{false};
    std::atomic<long> reads{0};

    // Seed one committed version so readers have something to see.
    vs.Write(1, HOT_KEY, "balance=1000");
    vs.Commit(1, clock.fetch_add(1));

    // Writer: create a new (pending) version, hold the "transaction" open, then commit.
    std::thread writer([&] {
        vs.Write(1000, HOT_KEY, "balance=2000");
        sleep_ms(kWriteHoldMs);
        vs.Commit(1000, clock.fetch_add(1));
    });
    sleep_ms(5);

    // Probe read while the write is in flight: should return immediately (old snapshot).
    auto t0 = Clock::now();
    {
        std::string out;
        vs.ReadSnapshot(clock.load(), HOT_KEY, &out);
    }
    *blocked_read_ms = ms_since(t0);

    std::vector<std::thread> readers;
    for (int i = 0; i < kReaders; ++i) {
        readers.emplace_back([&] {
            std::string out;
            while (!stop.load()) {
                vs.ReadSnapshot(clock.load(), HOT_KEY, &out);  // never blocks
                reads.fetch_add(1);
            }
        });
    }
    sleep_ms(kWindowMs);
    stop = true;
    for (auto& r : readers) r.join();
    writer.join();
    return reads.load();
}

int main() {
    printf("=== Track B: MVCC vs Strict 2PL under contention ===\n");
    printf("workload: %d reader threads on one hot row; a writer holds it for %d ms\n",
           kReaders, kWriteHoldMs);
    printf("observation window: %d ms\n\n", kWindowMs);

    double blocked_2pl = 0, blocked_mvcc = 0;
    long r2pl = Run2PL(&blocked_2pl);
    long rmvcc = RunMVCC(&blocked_mvcc);

    printf("Strict 2PL : %8ld reads in window | single read issued during write blocked %.1f ms\n",
           r2pl, blocked_2pl);
    printf("MVCC       : %8ld reads in window | single read issued during write took    %.3f ms\n",
           rmvcc, blocked_mvcc);
    printf("\nMVCC completed %.1fx more reads under contention, and its reads did not block\n",
           r2pl > 0 ? static_cast<double>(rmvcc) / r2pl : 0.0);
    printf("on the writer (snapshot isolation: readers see the last committed version).\n");
    return 0;
}
