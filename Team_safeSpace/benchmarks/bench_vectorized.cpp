// Track A benchmark: vectorized (batch-at-a-time) scan/filter/aggregate vs the
// row-at-a-time baseline, on the same data.
//
// Run:  ./build/bench_vectorized
#include <chrono>
#include <cstdio>
#include <string>

#include "engine/database.h"
#include "vectorized/vectorized_engine.h"

using namespace minidb;
using Clock = std::chrono::high_resolution_clock;

static double MsSince(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

int main() {
  const std::string f = "bench_vec.db";
  std::remove(f.c_str());
  std::remove((f + ".wal").c_str());

  const int kRows = 500000;
  Database db(f);
  db.Execute("CREATE TABLE t (id INT, v INT)");
  for (int base = 0; base < kRows; base += 100) {
    std::string sql = "INSERT INTO t VALUES ";
    for (int i = 0; i < 100; i++) {
      int id = base + i;
      sql += "(" + std::to_string(id) + "," + std::to_string(id * 2) + ")";
      if (i != 99) sql += ",";
    }
    db.Execute(sql);
  }

  TableHeap *heap = db.GetCatalog()->GetTableHeap("t");
  const Schema &schema = db.GetCatalog()->GetTable("t")->schema;
  VectorizedEngine ve(heap, db.GetBufferPool(), &schema);

  const int32_t threshold = kRows / 2;  // ~50% selectivity

  // Row-at-a-time baseline.
  auto t0 = Clock::now();
  long row_result = ve.RowAtATimeFilterSum(0, threshold, 1);
  double row_ms = MsSince(t0);

  // Vectorized.
  t0 = Clock::now();
  long vec_result = ve.FilterSum(0, threshold, 1);
  double vec_ms = MsSince(t0);

  std::printf("MiniDB Track A — vectorized vs row-at-a-time  (rows=%d, ~50%% selectivity)\n", kRows);
  std::printf("------------------------------------------------------------\n");
  std::printf("Row-at-a-time : %8.1f ms   (%.0f M rows/sec)\n", row_ms, kRows / row_ms / 1000.0);
  std::printf("Vectorized    : %8.1f ms   (%.0f M rows/sec)\n", vec_ms, kRows / vec_ms / 1000.0);
  std::printf("Speedup       : %8.2fx\n", row_ms / vec_ms);
  std::printf("Results match : %s  (row=%ld, vec=%ld)\n",
              row_result == vec_result ? "yes" : "NO", row_result, vec_result);

  std::remove(f.c_str());
  std::remove((f + ".wal").c_str());
  return 0;
}
