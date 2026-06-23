// Benchmark: index scan vs sequential scan for point queries, plus insert
// throughput and buffer-pool hit rate. Prints a small report to stdout.
//
// Run:  ./build/bench_index
#include <chrono>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "engine/database.h"

using namespace minidb;
using Clock = std::chrono::high_resolution_clock;

static double MsSince(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

int main() {
  const std::string f = "bench_index.db";
  std::remove(f.c_str());
  std::remove((f + ".wal").c_str());

  const int kRows = 50000;
  const int kQueries = 2000;

  Database db(f);
  db.Execute("CREATE TABLE t (id INT, v INT)");

  // ---- Insert throughput (batched, 100 rows per statement) ----------------
  auto t0 = Clock::now();
  for (int base = 0; base < kRows; base += 100) {
    std::string sql = "INSERT INTO t VALUES ";
    for (int i = 0; i < 100; i++) {
      int id = base + i;
      sql += "(" + std::to_string(id) + "," + std::to_string(id * 2) + ")";
      if (i != 99) sql += ",";
    }
    db.Execute(sql);
  }
  double insert_ms = MsSince(t0);

  // Deterministic set of probe ids.
  std::mt19937 rng(42);
  std::uniform_int_distribution<int> pick(0, kRows - 1);
  std::vector<int> probes(kQueries);
  for (int &p : probes) p = pick(rng);

  // ---- Sequential scan (no index) -----------------------------------------
  t0 = Clock::now();
  long seq_hits = 0;
  for (int id : probes) {
    seq_hits += db.Execute("SELECT v FROM t WHERE id = " + std::to_string(id)).affected;
  }
  double seq_ms = MsSince(t0);

  // ---- Index scan ----------------------------------------------------------
  db.Execute("CREATE INDEX t_id ON t (id)");
  t0 = Clock::now();
  long idx_hits = 0;
  for (int id : probes) {
    idx_hits += db.Execute("SELECT v FROM t WHERE id = " + std::to_string(id)).affected;
  }
  double idx_ms = MsSince(t0);

  std::printf("MiniDB micro-benchmark  (rows=%d, queries=%d)\n", kRows, kQueries);
  std::printf("------------------------------------------------------------\n");
  std::printf("Insert throughput : %8.1f ms   (%.0f rows/sec)\n", insert_ms,
              kRows / (insert_ms / 1000.0));
  std::printf("Sequential scan   : %8.1f ms   (%.3f ms/query, %ld hits)\n", seq_ms,
              seq_ms / kQueries, seq_hits);
  std::printf("Index scan        : %8.1f ms   (%.3f ms/query, %ld hits)\n", idx_ms,
              idx_ms / kQueries, idx_hits);
  std::printf("Speedup (seq/idx) : %8.1fx\n", seq_ms / idx_ms);
  std::printf("Buffer pool       : %zu hits / %zu misses (%.1f%% hit rate)\n",
              db.GetBufferPool()->Hits(), db.GetBufferPool()->Misses(),
              100.0 * db.GetBufferPool()->Hits() /
                  (db.GetBufferPool()->Hits() + db.GetBufferPool()->Misses() + 1));

  std::remove(f.c_str());
  std::remove((f + ".wal").c_str());
  return 0;
}
