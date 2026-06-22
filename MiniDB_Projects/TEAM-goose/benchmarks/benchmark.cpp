#include "../src/database.h"
#include <iostream>
#include <chrono>
#include <iomanip>

using namespace minidb;
using namespace std::chrono;

// ---------------------------------------------------------------------------
// MiniDB Benchmark Suite — Track C (LSM-tree vs B+tree storage comparison)
// ---------------------------------------------------------------------------

struct BenchmarkResult {
    std::string name;
    size_t      ops;
    double      elapsed_ms;
    double      ops_per_sec;
};

static BenchmarkResult run_bench(const std::string& name,
                                  const std::function<void()>& fn) {
    auto start = high_resolution_clock::now();
    fn();
    auto end   = high_resolution_clock::now();
    double ms  = duration_cast<microseconds>(end - start).count() / 1000.0;
    return {};
}

static void print_result(const BenchmarkResult& r, size_t ops) {
    double ops_sec = (r.elapsed_ms > 0) ? (ops * 1000.0 / r.elapsed_ms) : 0;
    std::cout << std::left << std::setw(30) << r.name
              << std::setw(10) << r.elapsed_ms << " ms"
              << std::setw(15) << static_cast<int>(ops_sec) << " ops/sec\n";
}

int main() {
    std::cout << "\n=== MiniDB Benchmark Suite ===\n";
    std::cout << "Track C — LSM-tree Storage Engine\n\n";

    // ---- Setup ----------------------------------------------------------
    Database db("benchmark_data");
    db.init(false); // skip recovery for benchmarks

    // Create test table
    db.execute("CREATE TABLE users (id INT, name STRING, age INT, PRIMARY KEY (id))");
    std::cout << "Table created.\n\n";

    // ---- 1. Write Throughput — Bulk Insert -------------------------------
    const size_t NUM_ROWS = 10000;
    std::cout << "--- Write Throughput (" << NUM_ROWS << " rows) ---\n";

    {
        auto start = high_resolution_clock::now();
        for (size_t i = 1; i <= NUM_ROWS; ++i) {
            std::string sql = "INSERT INTO users VALUES (" +
                              std::to_string(i) + ", 'user" +
                              std::to_string(i) + "', " +
                              std::to_string(20 + (i % 50)) + ")";
            db.execute(sql);
        }
        auto end = high_resolution_clock::now();
        double ms = duration_cast<microseconds>(end - start).count() / 1000.0;
        double ops_sec = (ms > 0) ? (NUM_ROWS * 1000.0 / ms) : 0;

        std::cout << std::left << std::setw(30) << "Bulk Insert"
                  << std::setw(10) << ms << " ms"
                  << std::setw(15) << static_cast<int>(ops_sec) << " ops/sec\n";
    }

    // ---- 2. Point Read Latency — Random Lookups -------------------------
    std::cout << "\n--- Point Read Latency (1000 lookups) ---\n";
    const size_t NUM_LOOKUPS = 1000;

    {
        auto start = high_resolution_clock::now();
        for (size_t i = 0; i < NUM_LOOKUPS; ++i) {
            size_t id = 1 + (i * 7) % NUM_ROWS; // pseudo-random
            db.execute("SELECT * FROM users WHERE id = " + std::to_string(id));
        }
        auto end = high_resolution_clock::now();
        double ms = duration_cast<microseconds>(end - start).count() / 1000.0;
        double ops_sec = (ms > 0) ? (NUM_LOOKUPS * 1000.0 / ms) : 0;
        double avg_us = (NUM_LOOKUPS > 0) ? (ms * 1000.0 / NUM_LOOKUPS) : 0;

        std::cout << std::left << std::setw(30) << "Point Read (avg)"
                  << std::setw(10) << avg_us << " μs"
                  << std::setw(15) << static_cast<int>(ops_sec) << " ops/sec\n";
    }

    // ---- 3. Range Scan — Sequential Scan --------------------------------
    std::cout << "\n--- Range Scan (500 ranges of 20 rows) ---\n";
    const size_t NUM_SCANS = 500;

    {
        auto start = high_resolution_clock::now();
        for (size_t i = 0; i < NUM_SCANS; ++i) {
            size_t low = 1 + (i * 19) % (NUM_ROWS - 20);
            db.execute("SELECT * FROM users WHERE id >= " + std::to_string(low) +
                       " AND id <= " + std::to_string(low + 19));
        }
        auto end = high_resolution_clock::now();
        double ms = duration_cast<microseconds>(end - start).count() / 1000.0;
        double ops_sec = (ms > 0) ? (NUM_SCANS * 1000.0 / ms) : 0;

        std::cout << std::left << std::setw(30) << "Range Scan"
                  << std::setw(10) << ms << " ms"
                  << std::setw(15) << static_cast<int>(ops_sec) << " scans/sec\n";
    }

    // ---- 4. Storage Amplification ---------------------------------------
    std::cout << "\n--- Storage Statistics ---\n";
    std::cout << "MemTable entries:   " << db.storage().memtable_size() << "\n";
    std::cout << "SSTable count:      " << db.storage().sstable_count() << "\n";

    // Count total SSTable file sizes
    size_t total_sst_bytes = 0;
    for (const auto& entry :
         std::filesystem::directory_iterator(db.storage().data_dir())) {
        if (entry.path().extension() == ".sst") {
            total_sst_bytes += std::filesystem::file_size(entry);
        }
    }
    std::cout << "SSTable total size: " << total_sst_bytes << " bytes ("
              << (total_sst_bytes / 1024.0) << " KB)\n";

    // ---- 5. Transaction Throughput — Concurrent Writes ------------------
    std::cout << "\n--- Transaction Throughput (100 txns, 10 inserts each) ---\n";

    {
        auto start = high_resolution_clock::now();
        for (size_t t = 0; t < 100; ++t) {
            TxnID tid = db.begin_transaction();
            for (size_t i = 0; i < 10; ++i) {
                size_t id = NUM_ROWS + t * 10 + i + 1;
                db.execute("INSERT INTO users VALUES (" + std::to_string(id) +
                           ", 'txn_user', 30)");
            }
            db.commit_transaction(tid);
        }
        auto end = high_resolution_clock::now();
        double ms = duration_cast<microseconds>(end - start).count() / 1000.0;
        double txn_sec = (ms > 0) ? (100.0 * 1000.0 / ms) : 0;

        std::cout << std::left << std::setw(30) << "Transactions"
                  << std::setw(10) << ms << " ms"
                  << std::setw(15) << static_cast<int>(txn_sec) << " txn/sec\n";
    }

    // ---- 6. JOIN Performance ---------------------------------------------
    std::cout << "\n--- JOIN Performance (100-row result) ---\n";
    db.execute("CREATE TABLE orders (oid INT, uid INT, amount FLOAT, PRIMARY KEY (oid))");

    for (size_t i = 1; i <= 100; ++i) {
        db.execute("INSERT INTO orders VALUES (" + std::to_string(i) + ", " +
                   std::to_string(1 + (i % 50)) + ", " +
                   std::to_string(100.0 + i * 10) + ")");
    }

    {
        auto start = high_resolution_clock::now();
        for (int i = 0; i < 50; ++i) {
            db.execute("SELECT users.name, orders.amount FROM users JOIN orders ON users.id = orders.uid");
        }
        auto end = high_resolution_clock::now();
        double ms = duration_cast<microseconds>(end - start).count() / 1000.0;

        std::cout << std::left << std::setw(30) << "Nested-Loop Join (50x)"
                  << std::setw(10) << ms << " ms\n";
    }

    // ---- Cleanup ---------------------------------------------------------
    db.shutdown();
    std::cout << "\n=== Benchmarks Complete ===\n";
    return 0;
}
