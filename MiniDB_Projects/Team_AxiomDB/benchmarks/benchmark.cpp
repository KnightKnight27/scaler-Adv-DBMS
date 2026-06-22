// ===========================================================================
// AxiomDB LSM vs Heap B+Tree Benchmark: heap-file + B+tree  vs  LSM-tree.
//
// The SAME workload is driven against both engines through the shared
// StorageEngine interface (that is the whole point of the abstraction), so the
// comparison is apples-to-apples.  We report exactly what the guidelines ask
// for: write throughput, read latency, and storage amplification -- plus write
// amplification and the effect of compaction on the LSM.
//
//   ./axiomdb_bench [N rows] [M point-lookups]
// ===========================================================================
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "engine/classic_storage_engine.h"
#include "engine/storage_engine.h"
#include "lsm/lsm_storage_engine.h"

namespace fs = std::filesystem;
using namespace axiomdb;
using Clock = std::chrono::steady_clock;

namespace {

constexpr size_t kValueSize = 96;  // bytes; with a ~11-byte key => ~107 B/row logical

std::string make_key(int i) {
  char b[16];
  std::snprintf(b, sizeof(b), "k%010d", i);
  return b;
}
std::string make_value(int i) {
  std::string v = "v" + std::to_string(i);
  v.resize(kValueSize, '.');
  return v;
}
double seconds_since(Clock::time_point t0) {
  return std::chrono::duration<double>(Clock::now() - t0).count();
}

struct Result {
  std::string engine;
  double load_ops = 0;       // puts/sec during bulk load
  double p50_us = 0, p99_us = 0;  // point-read latency
  double range_rows = 0;     // rows/sec on range scans
  double write_amp = 0;      // bytes physically written / logical bytes
  double storage_amp = 0;    // on-disk size / logical bytes
};

// Point-read latency percentiles over the given probe keys.
void measure_points(StorageEngine& eng, const std::vector<int>& probes, double& p50, double& p99) {
  std::vector<double> lat_us;
  lat_us.reserve(probes.size());
  for (int idx : probes) {
    std::string key = make_key(idx);
    auto t0 = Clock::now();
    volatile auto v = eng.get(key);
    lat_us.push_back(std::chrono::duration<double, std::micro>(Clock::now() - t0).count());
    (void)v;
  }
  std::sort(lat_us.begin(), lat_us.end());
  p50 = lat_us[lat_us.size() / 2];
  p99 = lat_us[(lat_us.size() * 99) / 100];
}

// Range-scan throughput: `count` scans of ~`width` keys each.
double measure_range(StorageEngine& eng, int n, int width, int count, std::mt19937& rng) {
  std::uniform_int_distribution<int> dist(0, n - width - 1);
  uint64_t rows = 0;
  auto t0 = Clock::now();
  for (int s = 0; s < count; ++s) {
    int lo = dist(rng);
    for (auto it = eng.scan(make_key(lo), make_key(lo + width)); it->valid(); it->next()) ++rows;
  }
  double secs = seconds_since(t0);
  return secs > 0 ? rows / secs : 0;
}

std::vector<int> random_probes(int n, int m, std::mt19937& rng) {
  std::uniform_int_distribution<int> dist(0, n - 1);
  std::vector<int> probes(m);
  for (int& p : probes) p = dist(rng);
  return probes;
}

}  // namespace

int main(int argc, char** argv) {
  int N = argc > 1 ? std::atoi(argv[1]) : 100000;
  int M = argc > 2 ? std::atoi(argv[2]) : 20000;
  const double logical_bytes = static_cast<double>(N) * (11 + kValueSize);

  std::string dir = "/tmp/axiomdb_bench_" + std::to_string(::getpid());
  fs::remove_all(dir);
  fs::create_directories(dir);

  std::mt19937 rng(42);
  std::vector<int> probes = random_probes(N, M, rng);

  std::printf("AxiomDB LSM vs Heap B+Tree Benchmark | N=%d rows, M=%d point-lookups, value=%zuB\n\n",
              N, M, kValueSize);

  std::vector<Result> results;

  // ---- Engine A: heap file + B+tree ----
  {
    ClassicStorageEngine eng(dir + "/heap.wdb");
    auto t0 = Clock::now();
    for (int i = 0; i < N; ++i) eng.put(make_key(i), make_value(i));
    eng.flush();
    Result r;
    r.engine = "HeapBTree";
    r.load_ops = N / seconds_since(t0);
    measure_points(eng, probes, r.p50_us, r.p99_us);
    r.range_rows = measure_range(eng, N, 100, 200, rng);
    r.write_amp = eng.bytes_written() / logical_bytes;
    r.storage_amp = eng.disk_size() / logical_bytes;
    results.push_back(r);
  }

  // ---- Engine B: LSM (measured BEFORE compaction, then AFTER) ----
  Result lsm_post;
  {
    LsmStorageEngine eng(dir + "/lsm");
    eng.set_compaction_threshold(1u << 20);  // hold off auto-compaction to expose amplification
    auto t0 = Clock::now();
    for (int i = 0; i < N; ++i) eng.put(make_key(i), make_value(i));
    eng.flush();
    Result r;
    r.engine = "LSM (pre-compaction)";
    r.load_ops = N / seconds_since(t0);
    measure_points(eng, probes, r.p50_us, r.p99_us);
    r.range_rows = measure_range(eng, N, 100, 200, rng);
    r.write_amp = eng.bytes_written() / logical_bytes;
    r.storage_amp = eng.disk_size() / logical_bytes;
    results.push_back(r);

    std::printf("  [LSM had %zu SSTables before compaction; compacting...]\n", eng.num_sstables());
    eng.compact();

    lsm_post.engine = "LSM (post-compaction)";
    lsm_post.load_ops = r.load_ops;  // same load
    measure_points(eng, probes, lsm_post.p50_us, lsm_post.p99_us);
    lsm_post.range_rows = measure_range(eng, N, 100, 200, rng);
    lsm_post.write_amp = eng.bytes_written() / logical_bytes;  // includes the compaction rewrite
    lsm_post.storage_amp = eng.disk_size() / logical_bytes;
    results.push_back(lsm_post);
  }

  // ---- Report ----
  auto print_row = [](const Result& r) {
    std::printf("| %-22s | %12.0f | %8.2f | %8.2f | %12.0f | %8.2fx | %9.2fx |\n",
                r.engine.c_str(), r.load_ops, r.p50_us, r.p99_us, r.range_rows, r.write_amp,
                r.storage_amp);
  };
  std::printf("\n| %-22s | %12s | %8s | %8s | %12s | %8s | %9s |\n", "engine", "load ops/s",
              "p50 us", "p99 us", "scan rows/s", "write-amp", "store-amp");
  std::printf("|%s|%s|%s|%s|%s|%s|%s|\n", "------------------------", "--------------",
              "----------", "----------", "--------------", "----------", "-----------");
  for (const Result& r : results) print_row(r);

  // CSV for the report / further analysis.
  std::ofstream csv("benchmark_results.csv");
  csv << "engine,load_ops_per_sec,point_p50_us,point_p99_us,scan_rows_per_sec,write_amp,storage_amp\n";
  for (const Result& r : results) {
    csv << r.engine << ',' << r.load_ops << ',' << r.p50_us << ',' << r.p99_us << ','
        << r.range_rows << ',' << r.write_amp << ',' << r.storage_amp << '\n';
  }
  std::printf("\nWrote benchmark_results.csv\n");

  fs::remove_all(dir);
  return 0;
}
