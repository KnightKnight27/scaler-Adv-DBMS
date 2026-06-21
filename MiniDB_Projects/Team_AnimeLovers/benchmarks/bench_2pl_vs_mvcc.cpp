// bench_2pl_vs_mvcc.cpp
//
// Benchmark: 2PL vs MVCC read throughput under concurrent read+write workload.
//
// Setup:
//   - Table with 1000 rows (id INT PRIMARY KEY, val VARCHAR)
//   - Writer thread inserts / deletes rows in a loop
//   - N reader threads execute SELECT * FROM bench WHERE id = <random>
//
// We measure reads/second in both modes and show that MVCC readers never
// block behind the writer, while 2PL readers must wait for the writer's lock.
//
// Build: compiled automatically by CMakeLists.txt as the `bench` target.
// Run:   ./build/bench [num_reader_threads] [duration_seconds]

#include "../src/engine.h"
#include "../src/transaction.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
namespace ch = std::chrono;

// ─── Helpers ─────────────────────────────────────────────────────────────────

static void setup_table(TransactionManager& tm) {
    auto txn = tm.begin();
    tm.execute(txn, "CREATE TABLE bench (id INT PRIMARY KEY, val VARCHAR(32))");
    tm.commit(txn);

    // Insert 1000 initial rows
    for (int i = 0; i < 1000; ++i) {
        auto t = tm.begin();
        tm.execute(t, "INSERT INTO bench VALUES (" + std::to_string(i) +
                   ", 'row_" + std::to_string(i) + "')");
        tm.commit(t);
    }
}

struct BenchResult {
    std::string mode;
    int         readers;
    double      duration_s;
    long long   total_reads;
    double      reads_per_sec;
    long long   total_writes;
    double      writes_per_sec;
    double      avg_read_latency_us;
};

// ─── Workload runner ──────────────────────────────────────────────────────────

static BenchResult run_benchmark(ConcurrencyMode mode, int num_readers, double duration_s,
                                  const std::string& db_dir) {
    // Fresh database for each run
    fs::remove_all(db_dir);
    fs::create_directories(db_dir);

    Database db(db_dir);
    TransactionManager tm(db, db_dir + "/bench.wal", mode);
    setup_table(tm);

    std::atomic<long long> read_count{0}, write_count{0};
    std::atomic<long long> read_latency_us{0};
    std::atomic<bool>      stop{false};

    auto deadline = ch::steady_clock::now() + ch::duration<double>(duration_s);

    // Writer thread: alternately inserts a new row and deletes it
    std::thread writer([&]() {
        int id = 2000;
        std::mt19937 rng(42);
        while (!stop.load(std::memory_order_relaxed)) {
            try {
                auto t = tm.begin();
                tm.execute(t, "INSERT INTO bench VALUES (" + std::to_string(id) +
                           ", 'w_" + std::to_string(id) + "')");
                tm.commit(t);

                auto t2 = tm.begin();
                tm.execute(t2, "DELETE FROM bench WHERE id = " + std::to_string(id));
                tm.commit(t2);

                ++id;
                ++write_count;
            } catch (...) {}
        }
    });

    // Reader threads: random point lookups
    std::vector<std::thread> readers;
    for (int r = 0; r < num_readers; ++r) {
        readers.emplace_back([&, r]() {
            std::mt19937 rng(r * 137 + 1);
            std::uniform_int_distribution<int> dist(0, 999);
            while (!stop.load(std::memory_order_relaxed)) {
                int key = dist(rng);
                try {
                    auto t0 = ch::steady_clock::now();
                    auto t  = tm.begin();
                    tm.execute(t, "SELECT * FROM bench WHERE id = " + std::to_string(key));
                    tm.commit(t);
                    auto t1 = ch::steady_clock::now();
                    read_latency_us.fetch_add(
                        ch::duration_cast<ch::microseconds>(t1 - t0).count(),
                        std::memory_order_relaxed);
                    read_count.fetch_add(1, std::memory_order_relaxed);
                } catch (...) {}
            }
        });
    }

    // Wait for deadline
    std::this_thread::sleep_until(deadline);
    stop.store(true);
    writer.join();
    for (auto& t : readers) t.join();

    long long rc = read_count.load();
    long long wc = write_count.load();
    double avg_lat = rc > 0 ? (double)read_latency_us.load() / rc : 0;

    return {
        mode == ConcurrencyMode::TWO_PL ? "2PL" : "MVCC",
        num_readers,
        duration_s,
        rc, rc / duration_s,
        wc, wc / duration_s,
        avg_lat
    };
}

// ─── Report printer ───────────────────────────────────────────────────────────

static void print_result(const BenchResult& r) {
    std::cout << std::left
              << std::setw(8)  << r.mode
              << std::setw(10) << r.readers
              << std::setw(14) << std::fixed << std::setprecision(0) << r.reads_per_sec
              << std::setw(14) << r.writes_per_sec
              << std::setw(16) << std::setprecision(1) << r.avg_read_latency_us
              << "\n";
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    int    num_readers = 4;
    double duration_s  = 5.0;
    if (argc > 1) num_readers = std::atoi(argv[1]);
    if (argc > 2) duration_s  = std::atof(argv[2]);

    std::cout << "\n=== MiniDB Benchmark: 2PL vs MVCC ===\n";
    std::cout << "Readers: " << num_readers
              << "  Duration: " << duration_s << "s each\n\n";
    std::cout << std::left
              << std::setw(8)  << "Mode"
              << std::setw(10) << "Readers"
              << std::setw(14) << "Reads/sec"
              << std::setw(14) << "Writes/sec"
              << std::setw(16) << "AvgLatency(us)"
              << "\n";
    std::cout << std::string(62, '-') << "\n";

    auto r2pl = run_benchmark(ConcurrencyMode::TWO_PL, num_readers, duration_s, "bench_2pl_data");
    print_result(r2pl);

    auto rmvcc = run_benchmark(ConcurrencyMode::MVCC, num_readers, duration_s, "bench_mvcc_data");
    print_result(rmvcc);

    std::cout << std::string(62, '-') << "\n";

    double speedup = (r2pl.reads_per_sec > 0) ? rmvcc.reads_per_sec / r2pl.reads_per_sec : 0;
    double lat_imp = (rmvcc.avg_read_latency_us > 0)
                     ? r2pl.avg_read_latency_us / rmvcc.avg_read_latency_us : 0;
    std::cout << "\nMVCC read throughput speedup vs 2PL : "
              << std::fixed << std::setprecision(2) << speedup << "x\n";
    std::cout << "MVCC average read latency improvement: "
              << lat_imp << "x lower latency\n\n";

    std::cout << "Interpretation:\n"
              << "  In 2PL mode, reader threads block while the writer holds its exclusive\n"
              << "  lock, reducing concurrent read throughput.\n"
              << "  In MVCC mode, each reader sees a consistent snapshot taken at BEGIN\n"
              << "  time, so reads proceed without blocking regardless of concurrent writes.\n\n";

    // Cleanup
    fs::remove_all("bench_2pl_data");
    fs::remove_all("bench_mvcc_data");
    return 0;
}
