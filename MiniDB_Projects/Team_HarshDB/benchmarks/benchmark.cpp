// ---------------------------------------------------------------------------
// benchmark.cpp - performance experiments for MiniDB.
//
//   1. Index scan vs sequential scan: point lookups on an indexed column vs an
//      unindexed column of identical selectivity. Shows why the optimizer's
//      access-path choice matters.
//   2. MVCC vs 2PL read throughput under a concurrent writer: the core
//      demonstration for extension Track B - MVCC readers don't block on the
//      writer's lock, 2PL readers do.
//   3. Raw write (INSERT) throughput.
//
// Run: ./minidb_bench
// ---------------------------------------------------------------------------
#include "../src/database.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <vector>
#include <random>
#include <cstdio>

using namespace minidb;
using Clock = std::chrono::high_resolution_clock;

static double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

static void fresh(const std::string& prefix) {
    std::remove((prefix + ".db").c_str());
    std::remove((prefix + ".wal").c_str());
    std::remove((prefix + ".catalog").c_str());
}

// ---------------------------------------------------------------------------
static void bench_scan() {
    std::cout << "\n=== Benchmark 1: Index Scan vs Sequential Scan ===\n";
    const std::string prefix = "/tmp/minidb_bench_scan";
    fresh(prefix);
    Database db(prefix, /*mvcc=*/true, /*buffer_pages=*/4096);

    const int N = 20000;
    db.run_autocommit("CREATE TABLE bench (id INT PRIMARY KEY, val INT)");
    auto t0 = Clock::now();
    for (int i = 1; i <= N; ++i)
        db.run_autocommit("INSERT INTO bench VALUES (" + std::to_string(i) + ", " + std::to_string(i) + ")");
    std::cout << "loaded " << N << " rows in " << (long)ms_since(t0) << " ms\n";

    const int K = 300;
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> pick(1, N);
    std::vector<int> keys(K);
    for (auto& k : keys) k = pick(rng);

    // Index scan: WHERE id = k  (id is the primary key -> B+ tree)
    auto ti = Clock::now();
    for (int k : keys) db.run_autocommit("SELECT val FROM bench WHERE id = " + std::to_string(k));
    double idx_ms = ms_since(ti);

    // Sequential scan: WHERE val = k  (val is unindexed -> full heap scan)
    auto ts = Clock::now();
    for (int k : keys) db.run_autocommit("SELECT id FROM bench WHERE val = " + std::to_string(k));
    double seq_ms = ms_since(ts);

    std::cout << "  " << K << " point lookups via INDEX scan: " << idx_ms << " ms ("
              << (idx_ms / K) << " ms/query)\n";
    std::cout << "  " << K << " point lookups via SEQ   scan: " << seq_ms << " ms ("
              << (seq_ms / K) << " ms/query)\n";
    std::cout << "  -> index scan is ~" << (seq_ms / idx_ms) << "x faster on this workload\n";
    db.close();
}

// ---------------------------------------------------------------------------
static double run_read_throughput(bool mvcc) {
    const std::string prefix = "/tmp/minidb_bench_conc";
    fresh(prefix);
    Database db(prefix, mvcc, /*buffer_pages=*/4096);
    db.set_mvcc(mvcc);
    db.run_autocommit("CREATE TABLE hot (id INT PRIMARY KEY, v INT)");
    db.run_autocommit("INSERT INTO hot VALUES (1, 100)");

    std::atomic<bool> stop{false};
    std::atomic<long> reads{0};

    // Writer: keeps the hot row almost continuously locked by holding an
    // EXCLUSIVE lock ~1ms per transaction with only a tiny gap between them.
    // Under 2PL this stalls readers (they need a SHARED lock on the same row);
    // under MVCC readers ignore the lock and use their snapshot.
    std::thread writer([&]() {
        while (!stop.load()) {
            try {
                TxId w = db.begin();
                db.run("DELETE FROM hot WHERE id = 1", w);
                db.run("INSERT INTO hot VALUES (1, 200)", w);
                std::this_thread::sleep_for(std::chrono::milliseconds(1)); // hold the lock
                db.commit(w);
            } catch (std::exception&) { /* deadlock/abort - ignore for bench */ }
        }
    });

    // Fixed measurement window: count how many reads each mode completes while
    // the writer hammers the same row.
    const int    READERS     = 4;
    const double DURATION_MS = 500.0;
    auto t0 = Clock::now();
    std::vector<std::thread> rs;
    for (int r = 0; r < READERS; ++r) {
        rs.emplace_back([&]() {
            while (!stop.load()) {
                try {
                    TxId t = db.begin();
                    db.run("SELECT v FROM hot WHERE id = 1", t);
                    db.commit(t);
                    reads.fetch_add(1);
                } catch (std::exception&) {}
            }
        });
    }
    std::this_thread::sleep_for(std::chrono::milliseconds((int)DURATION_MS));
    stop.store(true);
    for (auto& t : rs) t.join();
    double elapsed = ms_since(t0);
    writer.join();
    db.close();

    double per_sec = reads.load() / (elapsed / 1000.0);
    std::cout << "  " << (mvcc ? "MVCC" : "2PL ") << " mode: " << reads.load()
              << " reads in " << (long)elapsed << " ms -> "
              << (long)per_sec << " reads/sec\n";
    return per_sec;
}

static void bench_concurrency() {
    std::cout << "\n=== Benchmark 2: MVCC vs 2PL read throughput (1 writer + 4 readers on a hot row) ===\n";
    double mvcc = run_read_throughput(true);
    double twopl = run_read_throughput(false);
    std::cout << "  -> MVCC sustains ~" << (mvcc / twopl)
              << "x the read throughput of 2PL because readers never block on the writer's lock\n";
}

// ---------------------------------------------------------------------------
static void bench_writes() {
    std::cout << "\n=== Benchmark 3: Write (INSERT) throughput ===\n";
    const std::string prefix = "/tmp/minidb_bench_write";
    fresh(prefix);
    Database db(prefix, /*mvcc=*/true, /*buffer_pages=*/4096);
    db.run_autocommit("CREATE TABLE w (id INT PRIMARY KEY, payload TEXT)");

    const int N = 20000;
    auto t0 = Clock::now();
    TxId t = db.begin();
    for (int i = 1; i <= N; ++i)
        db.run("INSERT INTO w VALUES (" + std::to_string(i) + ", 'row_payload_data')", t);
    db.commit(t);
    double elapsed = ms_since(t0);
    std::cout << "  inserted " << N << " rows in " << (long)elapsed << " ms -> "
              << (long)(N / (elapsed / 1000.0)) << " inserts/sec (single transaction)\n";
    db.close();
}

int main() {
    std::cout << std::unitbuf;
    std::cout << "MiniDB Benchmark Suite\n----------------------";
    bench_scan();
    bench_concurrency();
    bench_writes();
    std::cout << "\nBenchmarks complete.\n";
    return 0;
}
