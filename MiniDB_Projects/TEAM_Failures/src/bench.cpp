// ============================================================================
// bench.cpp  --  Benchmark for EXTENSION TRACK C: LSM-tree vs B+ Tree storage.
//
// Measures, for the same workload:
//   * write throughput  (inserts per second)
//   * read latency      (average nanoseconds per point lookup)
//   * space amplification (LSM only: physical entries vs logical keys, before
//     and after compaction)
//
// Run:  ./build/bench [N]      (default N = 200000 keys)
// ============================================================================
#include <chrono>
#include <filesystem>
#include <random>
#include "common/common.h"
#include "index/bplus_tree.h"
#include "lsm/lsm_tree.h"

using namespace minidb;
using Clock = chrono::high_resolution_clock;
static double ms(Clock::time_point a, Clock::time_point b) {
  return chrono::duration<double, milli>(b - a).count();
}

int main(int argc, char **argv) {
  int N = (argc > 1) ? atoi(argv[1]) : 200000;
  string dir = "bench_lsm";
  filesystem::create_directories(dir);

  // A reproducible shuffled set of keys so both engines see the same workload.
  vector<int64_t> keys(N);
  for (int i = 0; i < N; ++i) keys[i] = i;
  mt19937 rng(42);
  shuffle(keys.begin(), keys.end(), rng);
  string val(16, 'x');

  cout << "MiniDB storage benchmark  (N = " << N << " keys)\n";
  cout << "============================================================\n";

  // ---------------- WRITE THROUGHPUT ----------------
  BPlusTree btree(64);
  auto t0 = Clock::now();
  for (int i = 0; i < N; ++i) btree.insert(Value((int32_t)keys[i]), RID(keys[i], 0));
  auto t1 = Clock::now();
  double btree_write_ms = ms(t0, t1);

  LSMTree lsm(dir, /*memtable_limit=*/10000, /*compaction_trigger=*/4);
  auto t2 = Clock::now();
  for (int i = 0; i < N; ++i) lsm.put(keys[i], val);
  auto t3 = Clock::now();
  double lsm_write_ms = ms(t2, t3);

  cout << "\n[WRITE THROUGHPUT]\n";
  cout << "  B+ Tree : " << (long)(N / (btree_write_ms / 1000.0)) << " inserts/sec ("
            << btree_write_ms << " ms)\n";
  cout << "  LSM     : " << (long)(N / (lsm_write_ms / 1000.0)) << " puts/sec    ("
            << lsm_write_ms << " ms)\n";

  // ---------------- READ LATENCY ----------------
  int M = min(N, 50000);
  vector<int64_t> probe(M);
  for (int i = 0; i < M; ++i) probe[i] = keys[(i * 7919) % N];

  auto t4 = Clock::now();
  long bhits = 0;
  for (int i = 0; i < M; ++i) { RID r; if (btree.search(Value((int32_t)probe[i]), &r)) bhits++; }
  auto t5 = Clock::now();

  auto t6 = Clock::now();
  long lhits = 0;
  for (int i = 0; i < M; ++i) { string o; if (lsm.get(probe[i], &o)) lhits++; }
  auto t7 = Clock::now();

  cout << "\n[READ LATENCY]  (" << M << " random point lookups)\n";
  cout << "  B+ Tree : " << (long)(ms(t4, t5) * 1e6 / M) << " ns/lookup (" << bhits << " hits)\n";
  cout << "  LSM     : " << (long)(ms(t6, t7) * 1e6 / M) << " ns/lookup (" << lhits << " hits)\n";

  // ---------------- SPACE & READ AMPLIFICATION (LSM) ----------------
  // Use a tree that does NOT auto-compact (high trigger) so many SSTables pile
  // up, then write every key TWICE.  Old versions linger in older SSTables.
  LSMTree lsm2(dir, /*memtable_limit=*/10000, /*compaction_trigger=*/100000);
  for (int round = 0; round < 2; ++round)
    for (int i = 0; i < N; ++i) lsm2.put(keys[i], val);
  lsm2.flush();
  LSMStats before = lsm2.stats();

  auto r0 = Clock::now();
  for (int i = 0; i < M; ++i) { string o; lsm2.get(probe[i], &o); }
  auto r1 = Clock::now();

  lsm2.compact();
  LSMStats after = lsm2.stats();

  auto r2 = Clock::now();
  for (int i = 0; i < M; ++i) { string o; lsm2.get(probe[i], &o); }
  auto r3 = Clock::now();

  cout << "\n[SPACE AMPLIFICATION]  (each key written twice; " << N << " logical keys)\n";
  cout << "  before compaction : " << before.sstable_entries << " physical entries in "
            << before.num_sstables << " SSTables  (~"
            << (double)before.sstable_entries / N << "x space)\n";
  cout << "  after  compaction : " << after.sstable_entries << " physical entries in "
            << after.num_sstables << " SSTable   (~"
            << (double)after.sstable_entries / N << "x space)\n";

  cout << "\n[READ AMPLIFICATION]  (lookup cost vs number of SSTables)\n";
  cout << "  with " << before.num_sstables << " SSTables : "
            << (long)(ms(r0, r1) * 1e6 / M) << " ns/lookup\n";
  cout << "  after compaction (1 SSTable) : " << (long)(ms(r2, r3) * 1e6 / M) << " ns/lookup\n";

  filesystem::remove_all(dir);
  cout << "\n(Interpretation: LSM favors write throughput and sequential I/O;\n"
               " the B+ Tree favors point-read latency. See benchmarks/REPORT.md.)\n";
  return 0;
}
