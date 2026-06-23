// query_benchmark.cpp — Track 4 (The Scribe: Benchmarks)
//
// Drives the Track 3 query engine end-to-end and measures throughput/latency
// of the two core paths the brief calls out:
//
//   * INSERT  — N rows pushed through the Insert operator (each appends to the
//               table and updates the primary-key B+ Tree index).
//   * SELECT  — N point lookups by primary key through the IndexScan operator
//               (B+ Tree range [k, k]).
//
// As a bonus it also times a full sequential TableScan so the README can
// contrast index lookups vs. a scan. All work runs under one transaction with
// the table-level 2PL lock held, so the numbers include real lock-manager
// overhead rather than a stripped-down hot path.
//
// Usage:  query_benchmark [N]      (default N = 10000)
#include <chrono>
#include <iostream>
#include <string>

#include "execution.h"

using namespace minidb;
using Clock = std::chrono::steady_clock;

static double ms_since(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

static void report(const std::string& label, long ops, double millis) {
  double per_sec = ops / (millis / 1000.0);
  double us_per_op = (millis * 1000.0) / ops;
  std::cout << "  " << label << "\n"
            << "    operations : " << ops << "\n"
            << "    total time : " << millis << " ms\n"
            << "    throughput : " << static_cast<long>(per_sec) << " ops/sec\n"
            << "    avg latency: " << us_per_op << " us/op\n\n";
}

int main(int argc, char** argv) {
  long N = 10000;
  if (argc > 1) N = std::stol(argv[1]);

  std::cout << "MiniDB query-engine benchmark (N = " << N << ")\n";
  std::cout << "----------------------------------------------\n";

  Catalog catalog;
  LockManager lock_mgr;
  ExecContext ctx{&lock_mgr, /*txn=*/1};

  Table* users = catalog.createTable(
      "users", {{"id", ValueType::Int}, {"name", ValueType::Text}},
      /*pk_index=*/0);

  // --- INSERT benchmark ---
  auto t0 = Clock::now();
  for (long k = 0; k < N; ++k) {
    Insert ins(users, {Value::Int(k), Value::Text("user_" + std::to_string(k))},
               ctx);
    ins.open();
    Tuple sink;
    while (ins.next(sink)) {
    }
    ins.close();
  }
  double insert_ms = ms_since(t0);
  report("INSERT (heap append + B+ Tree index update)", N, insert_ms);

  // --- SELECT benchmark: point lookup by primary key ---
  long found = 0;
  t0 = Clock::now();
  for (long k = 0; k < N; ++k) {
    IndexScan scan(users, /*low=*/k, /*high=*/k, ctx);
    scan.open();
    Tuple row;
    if (scan.next(row)) ++found;
    scan.close();
  }
  double select_ms = ms_since(t0);
  report("SELECT WHERE id = k (IndexScan point lookup)", N, select_ms);

  // --- Bonus: full sequential scan of all N rows ---
  t0 = Clock::now();
  long scanned = 0;
  {
    TableScan scan(users, ctx);
    scan.open();
    Tuple row;
    while (scan.next(row)) ++scanned;
    scan.close();
  }
  double scan_ms = ms_since(t0);
  report("SELECT * (full TableScan)", scanned, scan_ms);

  lock_mgr.release_all(ctx.txn);

  // Sanity: every inserted key must be retrievable.
  if (found != N || scanned != N) {
    std::cerr << "BENCHMARK SANITY FAILED: found=" << found
              << " scanned=" << scanned << " expected=" << N << "\n";
    return 1;
  }
  std::cout << "Sanity OK: all " << N << " rows inserted, looked up, scanned.\n";
  return 0;
}
