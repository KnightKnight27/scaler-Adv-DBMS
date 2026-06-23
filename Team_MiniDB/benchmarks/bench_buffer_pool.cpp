// Benchmark: effect of buffer pool size on a repeated full-scan workload.
// A small pool forces constant eviction/re-read from disk across passes
// over a table bigger than the pool; a pool that comfortably holds the
// whole table serves every later pass entirely from memory.
#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "catalog/catalog.h"
#include "query/executor.h"
#include "sql/lexer.h"
#include "sql/parser.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"

using namespace minidb;
using Clock = std::chrono::steady_clock;

QueryResult Run(Executor &ex, const std::string &sql) {
  Lexer lex(sql);
  Parser parser(lex.Tokenize());
  Statement stmt = parser.ParseStatement();
  return ex.Execute(stmt);
}

double Millis(Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); }

void RunTrial(size_t pool_frames, int n_rows, int passes) {
  std::string db = "bench_bpm_tmp.db";
  std::remove(db.c_str());
  std::remove((db + ".wal").c_str());
  std::remove("bench_bpm_tmp.meta");

  DiskManager dm(db);
  BufferPool bpm(pool_frames, &dm);
  Catalog cat(&bpm, "bench_bpm_tmp.meta");
  Executor ex(&cat);

  Run(ex, "CREATE TABLE wide (id INT PRIMARY KEY, payload VARCHAR)");
  for (int i = 0; i < n_rows; i++) {
    Run(ex, "INSERT INTO wide VALUES (" + std::to_string(i) + ", 'this-is-some-payload-text-" + std::to_string(i) + "')");
  }

  auto t0 = Clock::now();
  for (int p = 0; p < passes; p++) Run(ex, "SELECT * FROM wide");
  auto t1 = Clock::now();
  printf("pool_frames=%-6zu total_time=%.2fms (%d full scans of %d rows)\n", pool_frames, Millis(t1 - t0), passes, n_rows);

  std::remove(db.c_str());
  std::remove((db + ".wal").c_str());
  std::remove("bench_bpm_tmp.meta");
}

int main() {
  const int n_rows = 4000;   // heap spans well over a hundred pages
  const int passes = 20;
  printf("Repeated full-scan latency vs. buffer pool size (%d rows, %d passes):\n", n_rows, passes);
  RunTrial(4, n_rows, passes);     // pool much smaller than table -> constant eviction
  RunTrial(64, n_rows, passes);    // moderate
  RunTrial(1024, n_rows, passes);  // comfortably holds the whole table after pass 1
  return 0;
}
