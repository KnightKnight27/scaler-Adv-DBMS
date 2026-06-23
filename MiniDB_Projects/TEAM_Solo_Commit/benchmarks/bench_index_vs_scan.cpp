// Benchmark: B+Tree index point lookup vs full sequential scan as the table grows.
// Demonstrates why the cost-based optimizer prefers an index scan for selective equality.
#include <chrono>
#include <cstdio>
#include <string>

#include "database.h"

using namespace minidb;
using Clock = std::chrono::steady_clock;

static double Ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

int main() {
    std::remove("/tmp/minidb_bench_idx.db");
    Database db("/tmp/minidb_bench_idx.db", 256);
    db.Execute("CREATE TABLE t (id INT PRIMARY KEY, payload INT)");

    const int N = 50000;
    for (int i = 0; i < N; ++i)
        db.Execute("INSERT INTO t VALUES (" + std::to_string(i) + ", " + std::to_string(i * 2) + ")");

    const int probes = 2000;

    // Index path: equality on the primary key (optimizer picks IndexScan).
    auto a = Clock::now();
    long hits = 0;
    for (int i = 0; i < probes; ++i) {
        Result r = db.Execute("SELECT payload FROM t WHERE id = " + std::to_string((i * 37) % N));
        hits += r.rows.size();
    }
    double idx_ms = Ms(a, Clock::now());

    // Force a sequential scan by probing a non-indexed column.
    auto b = Clock::now();
    long hits2 = 0;
    for (int i = 0; i < probes; ++i) {
        Result r = db.Execute("SELECT id FROM t WHERE payload = " + std::to_string(((i * 37) % N) * 2));
        hits2 += r.rows.size();
    }
    double seq_ms = Ms(b, Clock::now());

    printf("=== Index vs Sequential Scan (N=%d rows, %d probes each) ===\n", N, probes);
    printf("IndexScan (WHERE id = ?)      : %.2f ms total, %.4f ms/query, %ld hits\n",
           idx_ms, idx_ms / probes, hits);
    printf("SeqScan   (WHERE payload = ?) : %.2f ms total, %.4f ms/query, %ld hits\n",
           seq_ms, seq_ms / probes, hits2);
    printf("Speedup from indexing         : %.1fx\n", seq_ms / idx_ms);
    return 0;
}
