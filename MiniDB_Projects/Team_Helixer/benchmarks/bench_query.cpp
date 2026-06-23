// Optimizer/index benchmark: index point scan vs sequential scan on the same
// table, demonstrating why the cost-based optimizer prefers the index.
//
//   ./bench_query [N]   (default N = 100000 rows, 2000 queries each)
#include <chrono>
#include <cstdio>
#include <random>
#include <string>
#include "engine/database.h"

using namespace minidb;
using clk = std::chrono::high_resolution_clock;
static double secs(clk::time_point a, clk::time_point b) {
    return std::chrono::duration<double>(b - a).count();
}

int main(int argc, char **argv) {
    int N = (argc > 1) ? atoi(argv[1]) : 100000;
    int Qi = 5000;  // index point queries (cheap)
    int Qs = 300;   // sequential scans (each touches every row, so far fewer)
    std::remove("bench_q.db"); std::remove("bench_q.wal");

    Database db("bench_q");
    db.execute("CREATE TABLE t (id INT, payload VARCHAR, k INT, PRIMARY KEY (id))");

    printf("=== Index scan vs Sequential scan  (N=%d rows; %d index, %d seq queries) ===\n\n", N, Qi, Qs);
    auto l0 = clk::now();
    // Bulk load inside one transaction to amortise WAL flushes.
    Transaction *t = db.begin();
    for (int i = 0; i < N; ++i)
        db.execute("INSERT INTO t VALUES (" + std::to_string(i) + ", 'row" +
                   std::to_string(i) + "', " + std::to_string(i % 1000) + ")", t);
    db.commit(t);
    auto l1 = clk::now();
    printf("Loaded %d rows in %.3fs (%.0f rows/s)\n\n", N, secs(l0, l1), N / secs(l0, l1));

    std::mt19937 rng(3);
    std::uniform_int_distribution<int> pick(0, N - 1);

    // Index point lookups: WHERE id = ?  -> INDEX_POINT.
    auto i0 = clk::now();
    for (int q = 0; q < Qi; ++q)
        db.execute("SELECT id FROM t WHERE id = " + std::to_string(pick(rng)));
    auto i1 = clk::now();
    double idx = secs(i0, i1) / Qi; // per-query

    // Sequential scans: WHERE k = ? (non-indexed column) -> SEQ_SCAN.
    auto s0 = clk::now();
    for (int q = 0; q < Qs; ++q)
        db.execute("SELECT id FROM t WHERE k = " + std::to_string(pick(rng) % 1000));
    auto s1 = clk::now();
    double seq = secs(s0, s1) / Qs; // per-query

    printf("INDEX_POINT (WHERE id=?) : %8.1f us/query\n", idx * 1e6);
    printf("SEQ_SCAN    (WHERE k=?)  : %8.1f us/query\n", seq * 1e6);
    printf("\nIndex scan is %.1fx faster than sequential scan on this table.\n", seq / idx);

    std::remove("bench_q.db"); std::remove("bench_q.wal");
    return 0;
}
