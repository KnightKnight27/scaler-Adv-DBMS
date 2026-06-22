// Benchmark: MVCC vs strict 2PL read throughput under write contention.
//
// A pool of reader threads runs read-only transactions while a pool of writer
// threads continuously updates the same keys. Under 2PL, readers take shared
// locks and block whenever a writer holds the exclusive lock; under MVCC,
// readers use their snapshot and never block. We measure how many read
// operations complete in a fixed wall-clock window in each mode.
#include "../src/txn/transaction_manager.hpp"
#include <atomic>
#include <thread>
#include <vector>
#include <chrono>
#include <cstdio>
#include <random>

using namespace minidb;
using clk = std::chrono::steady_clock;

static constexpr int KEYS = 64;
static constexpr int READERS = 8;
static constexpr int WRITERS = 4;
static constexpr int KEYS_PER_TXN = 4;   // locks held per transaction
static constexpr int RUN_MS = 1000;

struct Result {
    long reads = 0;
    long writes = 0;
    long read_deadlocks = 0;   // reader transactions aborted by a deadlock
    long deadlocks = 0;        // total waits-for cycles detected (both modes)
    long write_conflicts = 0;  // MVCC first-committer-wins write conflicts
};

static Result runMode(TransactionManager::Mode mode) {
    TransactionManager tm(mode);
    for (int k = 0; k < KEYS; k++) tm.setInitial(k, 0);

    std::atomic<bool> stop{false};
    std::atomic<long> reads{0}, writes{0}, rdl{0}, wconf{0};

    auto reader = [&](int seed) {
        std::mt19937 rng(seed);
        while (!stop.load(std::memory_order_relaxed)) {
            Txn t = tm.begin();
            try {
                for (int j = 0; j < KEYS_PER_TXN; j++) {
                    volatile int64_t v = tm.read(t, rng() % KEYS);
                    (void)v;
                    reads.fetch_add(1, std::memory_order_relaxed);
                }
                tm.commit(t);
            } catch (DeadlockAbort&) { tm.abort(t); rdl.fetch_add(1); }
        }
    };
    auto writer = [&](int seed) {
        std::mt19937 rng(seed);
        while (!stop.load(std::memory_order_relaxed)) {
            Txn t = tm.begin();
            try {
                int base = rng() % KEYS;
                for (int j = 0; j < KEYS_PER_TXN; j++) {
                    tm.write(t, (base + j) % KEYS, rng());
                    // Simulate doing work while holding the lock. Under 2PL this
                    // is exactly when readers of these keys are forced to wait;
                    // under MVCC readers proceed against their snapshot.
                    for (volatile int spin = 0; spin < 2000; spin++) {}
                }
                tm.commit(t); // may throw TxnConflict under MVCC first-committer-wins
                writes.fetch_add(1, std::memory_order_relaxed);
            } catch (DeadlockAbort&) { tm.abort(t); }
              catch (TxnConflict&)  { tm.abort(t); wconf.fetch_add(1); }
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < WRITERS; i++) threads.emplace_back(writer, 100 + i);
    for (int i = 0; i < READERS; i++) threads.emplace_back(reader, 1 + i);

    std::this_thread::sleep_for(std::chrono::milliseconds(RUN_MS));
    stop.store(true);
    for (auto& th : threads) th.join();

    return {reads.load(), writes.load(), rdl.load(),
            static_cast<long>(tm.deadlockCount()), wconf.load()};
}

int main() {
    std::printf("MiniDB concurrency benchmark (Track B: MVCC vs 2PL)\n");
    std::printf("config: keys=%d readers=%d writers=%d keys/txn=%d window=%dms\n\n",
                KEYS, READERS, WRITERS, KEYS_PER_TXN, RUN_MS);

    Result pl = runMode(TransactionManager::Mode::TWO_PL);
    Result mv = runMode(TransactionManager::Mode::MVCC);

    auto rps = [](long reads) { return reads * 1000.0 / RUN_MS; };
    std::printf("%-6s | %10s | %10s | %8s | %10s | %9s | %9s\n",
                "mode", "reads", "reads/sec", "writes", "deadlocks", "rd-blocks", "wr-confl");
    std::printf("-------+------------+------------+----------+------------+-----------+----------\n");
    std::printf("%-6s | %10ld | %10.0f | %8ld | %10ld | %9ld | %9ld\n",
                "2PL", pl.reads, rps(pl.reads), pl.writes, pl.deadlocks, pl.read_deadlocks, pl.write_conflicts);
    std::printf("%-6s | %10ld | %10.0f | %8ld | %10ld | %9ld | %9ld\n",
                "MVCC", mv.reads, rps(mv.reads), mv.writes, mv.deadlocks, mv.read_deadlocks, mv.write_conflicts);
    std::printf("\nMVCC read throughput speedup: %.2fx\n",
                pl.reads > 0 ? (double)mv.reads / pl.reads : 0.0);
    std::printf("Reader transactions aborted by deadlock: 2PL=%ld  MVCC=%ld\n",
                pl.read_deadlocks, mv.read_deadlocks);
    std::printf("(MVCC readers take no locks: they never block or deadlock. MVCC write\n"
                " conflicts are first-committer-wins aborts that prevent lost updates.)\n");
    return 0;
}
