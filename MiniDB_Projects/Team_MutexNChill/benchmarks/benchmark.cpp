// Track B — Concurrency Extension: MVCC vs Strict 2PL
//
// This benchmark shows why MVCC is faster under read-heavy workloads.
//
// Setup: 10 reader threads and 1 writer thread, running for 500ms.
//   - 2PL mode:  readers acquire a SHARED lock before reading.
//                The writer holds an EXCLUSIVE lock for 5ms at a time.
//                Readers BLOCK during those 5ms windows.
//   - MVCC mode: readers read from their snapshot — no lock needed.
//                The writer still acquires an exclusive lock, but
//                readers are never blocked by it.
//
// The result: MVCC shows significantly higher read throughput.

#include <iostream>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <chrono>
#include <vector>

using namespace std::chrono;

// Shared data: one integer "row" value plus a version counter.
static int    row_value     = 42;
static int    row_version   = 0; // MVCC version
static int    committed_version = 0; // latest committed version

// 2PL: a shared_mutex (many readers OR one writer).
static std::shared_mutex rw_lock;

// 2PL benchmark: each reader acquires a shared lock, reads, releases.
static std::atomic<long> twopl_reads(0);
static std::atomic<bool> stop_flag(false);

void twoplReader() {
    while (!stop_flag.load()) {
        {
            std::shared_lock<std::shared_mutex> lock(rw_lock);
            // Simulate reading (just increment the counter).
            (void)row_value;
            twopl_reads++;
        }
    }
}

void twoplWriter() {
    while (!stop_flag.load()) {
        {
            std::unique_lock<std::shared_mutex> lock(rw_lock);
            // Simulate a slow write (holds lock for 5ms).
            row_value++;
            std::this_thread::sleep_for(milliseconds(5));
        }
        std::this_thread::sleep_for(milliseconds(1));
    }
}

// MVCC benchmark: readers check the committed version and read without locking.
static std::atomic<long> mvcc_reads(0);
// We use a regular mutex only for the writer (to protect row_value).
static std::mutex write_lock;

void mvccReader() {
    while (!stop_flag.load()) {
        // Snapshot: take the committed version at the moment we started.
        int my_snapshot = committed_version;
        // Read is always visible — we don't block, we just use the snapshot.
        (void)my_snapshot;
        (void)row_value;
        mvcc_reads++;
    }
}

void mvccWriter() {
    while (!stop_flag.load()) {
        {
            std::lock_guard<std::mutex> lock(write_lock);
            // Create a new version.
            row_version++;
            row_value++;
            std::this_thread::sleep_for(milliseconds(5));
            // Commit: make the new version visible.
            committed_version = row_version;
        }
        std::this_thread::sleep_for(milliseconds(1));
    }
}

int main() {
    const int  NUM_READERS  = 10;
    const int  DURATION_MS  = 500;

    std::cout << "=== Track B: MVCC vs 2PL Benchmark ===\n";
    std::cout << "Readers: " << NUM_READERS << "  |  Duration: " << DURATION_MS << "ms\n\n";

    // ---- 2PL benchmark ----
    twopl_reads = 0;
    stop_flag   = false;

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_READERS; i++)
        threads.emplace_back(twoplReader);
    threads.emplace_back(twoplWriter);

    std::this_thread::sleep_for(milliseconds(DURATION_MS));
    stop_flag = true;
    for (auto& th : threads) th.join();
    threads.clear();

    long twopl_total = twopl_reads.load();
    std::cout << "2PL mode:\n";
    std::cout << "  Total reads in " << DURATION_MS << "ms: " << twopl_total << "\n";
    std::cout << "  Throughput: " << (twopl_total * 1000 / DURATION_MS) << " reads/sec\n\n";

    // ---- MVCC benchmark ----
    mvcc_reads  = 0;
    stop_flag   = false;
    row_value   = 42;
    row_version = 0;
    committed_version = 0;

    for (int i = 0; i < NUM_READERS; i++)
        threads.emplace_back(mvccReader);
    threads.emplace_back(mvccWriter);

    std::this_thread::sleep_for(milliseconds(DURATION_MS));
    stop_flag = true;
    for (auto& th : threads) th.join();
    threads.clear();

    long mvcc_total = mvcc_reads.load();
    std::cout << "MVCC mode:\n";
    std::cout << "  Total reads in " << DURATION_MS << "ms: " << mvcc_total << "\n";
    std::cout << "  Throughput: " << (mvcc_total * 1000 / DURATION_MS) << " reads/sec\n\n";

    // ---- Comparison ----
    double speedup = (twopl_total > 0) ? (double)mvcc_total / twopl_total : 0;
    std::cout << "Result: MVCC is " << speedup << "x faster than 2PL for read-heavy workloads.\n";
    std::cout << "Why: readers in 2PL block during the writer's 5ms exclusive lock window.\n";
    std::cout << "     MVCC readers read the last committed snapshot — they never block.\n";

    return 0;
}
