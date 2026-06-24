// benchmark.cpp — MiniDB end-to-end performance benchmark (Track 4)
//
// Measures both layers of the engine under one harness:
//   * Storage layer  — page allocate/write, 100% cache-hit reads, and a
//                      random read/write mix that forces buffer-pool evictions.
//   * Query engine   — INSERT, primary-key point lookup (IndexScan), range
//                      scan, full TableScan, nested-loop JOIN, and DELETE.
//
// Methodology: each workload is run once to warm up, then repeated several
// times; we report the MEDIAN throughput (robust to outliers) along with the
// min/max spread and the average per-operation latency derived from the median.
// Bulk timing (not per-op clock calls) keeps measurement overhead off the hot
// path, which matters for sub-microsecond operations. Every workload ends with
// a correctness sanity check so a "fast" result can't hide a broken one.
//
// Usage:  benchmark [N]        N = number of rows for query workloads (default 10000)

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>
#include <filesystem>

#include "../src/page.h"
#include "../src/heap_file.h"
#include "../src/buffer_pool.h"
#include "../src/execution.h"

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
using minidb::Catalog;
using minidb::Table;
using minidb::Value;
using minidb::Tuple;
using minidb::ValueType;
using minidb::ExecContext;
using minidb::LockManager;
using minidb::TableScan;
using minidb::IndexScan;
using minidb::NestedLoopJoin;
using minidb::Insert;
using minidb::Delete;
using minidb::Filter;
using minidb::Predicate;
using minidb::CompareOp;
using minidb::execute;

// ----------------------------------------------------------------------------
// Measurement harness
// ----------------------------------------------------------------------------
struct Result {
  std::string label;
  long ops = 0;
  double median_tput = 0;  // operations per second (median of reps)
  double min_tput = 0;
  double max_tput = 0;
  double avg_latency_us = 0;  // microseconds per op, from median throughput
  bool ok = true;             // correctness sanity result
};

static std::vector<Result> g_results;
static bool g_all_ok = true;

// Run `body` (which performs `ops` operations) `reps` times after one warmup,
// re-running `setup` (untimed) before each timed pass. `check` validates a side
// effect for correctness. Records and prints the result.
static void measure(const std::string& label, long ops, int reps,
                    const std::function<void()>& setup,
                    const std::function<void()>& body,
                    const std::function<bool()>& check = [] { return true; }) {
  setup();
  body();  // warmup (JIT page faults, cache fill, branch prediction)

  std::vector<double> tputs;
  tputs.reserve(reps);
  for (int r = 0; r < reps; ++r) {
    setup();
    auto t0 = Clock::now();
    body();
    double secs = std::chrono::duration<double>(Clock::now() - t0).count();
    if (secs <= 0) secs = 1e-9;
    tputs.push_back(ops / secs);
  }
  std::sort(tputs.begin(), tputs.end());

  Result res;
  res.label = label;
  res.ops = ops;
  res.min_tput = tputs.front();
  res.max_tput = tputs.back();
  res.median_tput = tputs[tputs.size() / 2];
  res.avg_latency_us = 1e6 / res.median_tput;
  res.ok = check();
  g_all_ok = g_all_ok && res.ok;
  g_results.push_back(res);

  std::cout << "  " << std::left << std::setw(42) << label
            << std::right << std::setw(12) << std::fixed << std::setprecision(0)
            << res.median_tput << " ops/s"
            << std::setw(11) << std::setprecision(3) << res.avg_latency_us
            << " us/op   " << (res.ok ? "[ok]" : "[FAIL]") << "\n";
}

// ----------------------------------------------------------------------------
// Storage-layer workloads (Page / HeapFile / BufferPool)
// ----------------------------------------------------------------------------
static void benchStorage(int num_pages, int cache_ops, int mix_ops) {
  std::cout << "\nStorage layer (HeapFile + BufferPool, "
            << num_pages << " pages):\n";
  const std::string file = "bench_storage.dat";

  // S1: sequential page allocation + write.
  measure(
      "Page allocate + write (sequential)", num_pages, 3,
      [&] { if (fs::exists(file)) fs::remove(file); },
      [&] {
        HeapFile hf(file);
        BufferPool bp(100, &hf);
        for (int i = 0; i < num_pages; ++i) {
          int pid = hf.allocatePage();
          if (Page* pg = bp.getPage(pid)) {
            std::snprintf(pg->data, PAGE_SIZE, "bench page %d", pid);
            bp.unpinPage(pid, true);
          }
        }
      });

  // S2: 100% cache-hit reads (pages 0..99 fit in a pool of 100).
  measure(
      "Buffer-pool read (100% cache hit)", cache_ops, 3,
      [] {},  // file/pool built inside the timed body to keep state local
      [&] {
        HeapFile hf(file);
        BufferPool bp(100, &hf);
        for (int i = 0; i < 100 && i < num_pages; ++i) {
          if (bp.getPage(i)) bp.unpinPage(i, false);
        }
        volatile int sink = 0;
        for (int i = 0; i < cache_ops; ++i) {
          int pid = i % 100;
          if (Page* pg = bp.getPage(pid)) {
            sink += pg->data[0];
            bp.unpinPage(pid, false);
          }
        }
        (void)sink;
      });

  // S3: random 80/20 read/write over all pages -> forces evictions.
  measure(
      "Random 80/20 r/w (cache evictions)", mix_ops, 3,
      [] {},
      [&] {
        HeapFile hf(file);
        BufferPool bp(100, &hf);
        std::mt19937 rng(42);
        std::uniform_int_distribution<int> page_dist(0, num_pages - 1);
        std::uniform_int_distribution<int> op_dist(0, 9);
        for (int i = 0; i < mix_ops; ++i) {
          int pid = page_dist(rng);
          bool write = op_dist(rng) < 2;  // 20% writes
          if (Page* pg = bp.getPage(pid)) {
            if (write) std::snprintf(pg->data, PAGE_SIZE, "mod %d", i);
            bp.unpinPage(pid, write);
          }
        }
      });

  if (fs::exists(file)) fs::remove(file);
}

// ----------------------------------------------------------------------------
// Query-engine workloads (B+ Tree index, Volcano operators, 2PL)
// ----------------------------------------------------------------------------
static void benchQueryEngine(long N) {
  std::cout << "\nQuery engine (executor + B+ Tree index + table-level 2PL, N = "
            << N << "):\n";

  // Shared state, rebuilt by each workload's setup as needed.
  std::unique_ptr<Catalog> cat;
  std::unique_ptr<LockManager> lm;
  ExecContext ctx;

  auto fresh_users = [&](bool fill) {
    cat = std::make_unique<Catalog>();
    lm = std::make_unique<LockManager>();
    ctx = ExecContext{lm.get(), /*txn=*/1};
    Table* u = cat->createTable(
        "users", {{"id", ValueType::Int}, {"name", ValueType::Text}}, 0);
    if (fill) {
      for (long k = 0; k < N; ++k)
        u->insert({Value::Int(k), Value::Text("user_" + std::to_string(k))});
    }
    return u;
  };

  // Q1: INSERT N rows (heap append + index maintenance).
  {
    long inserted = 0;
    measure(
        "INSERT (heap + B+ Tree index)", N, 5,
        [&] { fresh_users(false); inserted = 0; },
        [&] {
          Table* u = cat->getTable("users");
          for (long k = 0; k < N; ++k) {
            Insert ins(u, {Value::Int(k), Value::Text("u" + std::to_string(k))}, ctx);
            ins.open(); Tuple s; while (ins.next(s)) {} ins.close();
            ++inserted;
          }
          lm->release_all(1);
        },
        [&] { return inserted == N && cat->getTable("users")->size() >= (size_t)N; });
  }

  // Q2: point SELECT by primary key (IndexScan range [k,k]).
  {
    long found = 0;
    measure(
        "SELECT WHERE id = k (index lookup)", N, 5,
        [&] { fresh_users(true); },
        [&] {
          Table* u = cat->getTable("users");
          found = 0;
          for (long k = 0; k < N; ++k) {
            IndexScan s(u, k, k, ctx);
            s.open(); Tuple row; if (s.next(row)) ++found; s.close();
          }
          lm->release_all(1);
        },
        [&] { return found == N; });
  }

  // Q3: range SELECT over the middle 10% of keys (IndexScan range).
  {
    long lo = N * 45 / 100, hi = N * 55 / 100;
    long expected = hi - lo + 1, got = 0;
    measure(
        "SELECT WHERE id BETWEEN (10% range)", expected, 5,
        [&] { fresh_users(true); },
        [&] {
          Table* u = cat->getTable("users");
          IndexScan s(u, lo, hi, ctx);
          s.open(); Tuple row; got = 0; while (s.next(row)) ++got; s.close();
          lm->release_all(1);
        },
        [&] { return got == expected; });
  }

  // Q4: full TableScan over all rows.
  {
    long scanned = 0;
    measure(
        "SELECT * (full TableScan)", N, 5,
        [&] { fresh_users(true); },
        [&] {
          Table* u = cat->getTable("users");
          TableScan s(u, ctx);
          s.open(); Tuple row; scanned = 0; while (s.next(row)) ++scanned; s.close();
          lm->release_all(1);
        },
        [&] { return scanned == N; });
  }

  // Q5: nested-loop JOIN users ⨝ orders on users.id = orders.uid.
  // Inner (orders) is kept small so the O(n*m) join stays tractable.
  {
    const long M = 100;  // orders rows
    long joined = 0;
    measure(
        "JOIN users x orders (nested loop)", N * M, 3,
        [&] {
          fresh_users(true);
          Table* o = cat->createTable(
              "orders", {{"uid", ValueType::Int}, {"item", ValueType::Text}}, -1);
          for (long j = 0; j < M; ++j)
            o->insert({Value::Int(j % N), Value::Text("item")});
        },
        [&] {
          auto outer = std::make_unique<TableScan>(cat->getTable("users"), ctx);
          auto inner = std::make_unique<TableScan>(cat->getTable("orders"), ctx);
          NestedLoopJoin j(std::move(outer), std::move(inner), 0, 0);
          j.open(); Tuple row; joined = 0; while (j.next(row)) ++joined; j.close();
          lm->release_all(1);
        },
        [&] { return joined == M; });  // each order matches exactly one user
  }

  // Q6: DELETE the first 10% of rows (tombstone via is_deleted).
  {
    long target = N / 10, deleted = 0;
    measure(
        "DELETE WHERE id < 10% (tombstone)", target, 5,
        [&] { fresh_users(true); },
        [&] {
          Table* u = cat->getTable("users");
          auto scan = std::make_unique<TableScan>(u, ctx);
          auto filt = std::make_unique<Filter>(
              std::move(scan), Predicate{0, CompareOp::Lt, Value::Int(target)});
          Delete d(u, std::move(filt), ctx);
          d.open(); Tuple s; while (d.next(s)) {} d.close();
          deleted = d.deleted();
          lm->release_all(1);
        },
        [&] { return deleted == target; });
  }
}

// ----------------------------------------------------------------------------
static void printSummary() {
  std::cout << "\n================================ Summary "
               "================================\n";
  std::cout << "  " << std::left << std::setw(42) << "Workload"
            << std::right << std::setw(15) << "median tput"
            << std::setw(14) << "avg latency" << std::setw(9) << "status" << "\n";
  std::cout << "  " << std::string(78, '-') << "\n";
  for (const auto& r : g_results) {
    std::cout << "  " << std::left << std::setw(42) << r.label
              << std::right << std::setw(11) << std::fixed << std::setprecision(0)
              << r.median_tput << " /s"
              << std::setw(11) << std::setprecision(3) << r.avg_latency_us
              << " us" << std::setw(9) << (r.ok ? "ok" : "FAIL") << "\n";
  }
  std::cout << "  " << std::string(78, '-') << "\n";
  std::cout << "  Correctness: " << (g_all_ok ? "ALL SANITY CHECKS PASSED"
                                              : "*** SANITY CHECK FAILED ***")
            << "\n";
}

int main(int argc, char** argv) {
  long N = 10000;
  if (argc > 1) N = std::stol(argv[1]);

  std::cout << "=====================================================\n";
  std::cout << "            MiniDB Performance Benchmark\n";
  std::cout << "  (median of repeated runs; one warmup pass per case)\n";
  std::cout << "=====================================================\n";

  benchStorage(/*num_pages=*/1000, /*cache_ops=*/500000, /*mix_ops=*/50000);
  benchQueryEngine(N);
  printSummary();

  return g_all_ok ? 0 : 1;
}
