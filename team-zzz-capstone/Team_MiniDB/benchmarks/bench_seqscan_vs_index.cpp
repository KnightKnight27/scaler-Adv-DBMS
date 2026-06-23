// Benchmark: index scan vs. sequential scan for point lookups, at growing
// table sizes. Inserts N rows where `payload` is a unique string encoding
// of `id` but is NOT indexed, so an equality predicate on `payload` forces
// a full SeqScan even though it picks out exactly one row - the same
// logical lookup the PK IndexScan does in O(log N). This isolates the
// access-path cost the optimizer is choosing between.
#include <chrono>
#include <cstdio>
#include <iostream>
#include <random>
#include <vector>

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

double Microseconds(Clock::duration d) { return std::chrono::duration<double, std::micro>(d).count(); }

int main() {
  const std::vector<int> sizes = {1000, 5000, 20000};
  const int lookups = 200;
  std::mt19937 rng(42);

  printf("%-10s %-22s %-22s %-10s\n", "rows(N)", "IndexScan avg us/lookup", "SeqScan avg us/lookup", "speedup");
  for (int n : sizes) {
    std::string db = "bench_tmp.db";
    std::remove(db.c_str());
    std::remove((db + ".wal").c_str());
    std::remove("bench_tmp.meta");

    DiskManager dm(db);
    BufferPool bpm(256, &dm);
    Catalog cat(&bpm, "bench_tmp.meta");
    Executor ex(&cat);

    Run(ex, "CREATE TABLE bench (id INT PRIMARY KEY, payload VARCHAR)");
    for (int i = 0; i < n; i++) {
      Run(ex, "INSERT INTO bench VALUES (" + std::to_string(i) + ", 'p" + std::to_string(i) + "')");
    }

    std::uniform_int_distribution<int> dist(0, n - 1);
    std::vector<int> targets(lookups);
    for (auto &t : targets) t = dist(rng);

    auto t0 = Clock::now();
    for (int target : targets) Run(ex, "SELECT * FROM bench WHERE id = " + std::to_string(target));
    auto t1 = Clock::now();
    double index_us = Microseconds(t1 - t0) / lookups;

    t0 = Clock::now();
    for (int target : targets) Run(ex, "SELECT * FROM bench WHERE payload = 'p" + std::to_string(target) + "'");
    t1 = Clock::now();
    double seq_us = Microseconds(t1 - t0) / lookups;

    printf("%-10d %-22.2f %-22.2f %-10.1fx\n", n, index_us, seq_us, seq_us / index_us);

    std::remove(db.c_str());
    std::remove((db + ".wal").c_str());
    std::remove("bench_tmp.meta");
  }
  return 0;
}
