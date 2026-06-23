// MiniDB benchmark harness.
//   Usage: minidb_bench [rows]   (default 10000)
// Measures insert throughput and point-lookup latency with and without a
// B+Tree index (the index + optimizer payoff), plus delete throughput.
#include "minidb/database.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

using namespace minidb;
using Clock = std::chrono::steady_clock;

static double ms(Clock::duration d) {
  return std::chrono::duration<double, std::milli>(d).count();
}

int main(int argc, char** argv) {
  size_t N = argc > 1 ? static_cast<size_t>(std::strtoul(argv[1], nullptr, 10)) : 10000;
  if (N == 0) N = 10000;
  size_t K = N < 500 ? N : 500;  // number of point lookups

  std::remove("bench.data");
  std::remove("bench.catalog");
  std::remove("bench.wal");
  Database db("bench");
  db.execute("CREATE TABLE t (id INT, val INT);");

  // 1. Bulk insert inside one transaction.
  auto t0 = Clock::now();
  db.execute("BEGIN;");
  for (size_t i = 0; i < N; ++i)
    db.execute("INSERT INTO t VALUES (" + std::to_string(i) + ", " +
               std::to_string(i * 2) + ");");
  db.execute("COMMIT;");
  double insert_ms = ms(Clock::now() - t0);

  // Random keys to look up.
  std::mt19937 rng(12345);
  std::uniform_int_distribution<size_t> dist(0, N - 1);
  std::vector<size_t> keys(K);
  for (auto& k : keys) k = dist(rng);

  // 2. Point lookups with a sequential scan (no index yet).
  db.execute("ANALYZE t;");
  auto s0 = Clock::now();
  size_t found_seq = 0;
  for (size_t k : keys)
    found_seq += db.execute("SELECT val FROM t WHERE id = " + std::to_string(k) + ";").rows.size();
  double seq_ms = ms(Clock::now() - s0);

  // 3. Build an index, refresh statistics.
  db.execute("CREATE INDEX ix_id ON t(id);");
  db.execute("ANALYZE t;");

  // 4. Same lookups, now served by the index.
  auto i0 = Clock::now();
  size_t found_idx = 0;
  for (size_t k : keys)
    found_idx += db.execute("SELECT val FROM t WHERE id = " + std::to_string(k) + ";").rows.size();
  double idx_ms = ms(Clock::now() - i0);

  // 5. Delete throughput (remove the lower half).
  auto d0 = Clock::now();
  db.execute("BEGIN;");
  db.execute("DELETE FROM t WHERE id < " + std::to_string(N / 2) + ";");
  db.execute("COMMIT;");
  double delete_ms = ms(Clock::now() - d0);

  std::printf("\nMiniDB benchmark  (N = %zu rows, K = %zu point lookups)\n", N, K);
  std::printf("--------------------------------------------------------------\n");
  std::printf("%-30s %12s %16s\n", "scenario", "total (ms)", "throughput");
  std::printf("%-30s %12.1f %10.0f rows/s\n", "bulk insert", insert_ms,
              N / (insert_ms / 1000.0));
  std::printf("%-30s %12.1f %12.4f ms/op\n", "point lookup (seq scan)", seq_ms, seq_ms / K);
  std::printf("%-30s %12.1f %12.4f ms/op\n", "point lookup (index scan)", idx_ms, idx_ms / K);
  std::printf("%-30s %12.1f %10.0f rows/s\n", "delete (lower half)", delete_ms,
              (N / 2) / (delete_ms / 1000.0));
  std::printf("--------------------------------------------------------------\n");
  std::printf("index speedup on point lookups: %.1fx\n", seq_ms / (idx_ms > 0 ? idx_ms : 1e-9));
  (void)found_seq;
  (void)found_idx;
  return 0;
}
