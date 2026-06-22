// Track C benchmark: LSM-tree storage vs B+ tree (heap + index) row store.
// Measures write throughput, point-read latency, and storage amplification
// for both sequential and random key workloads.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "storage/disk_manager.h"
#include "storage/buffer_pool_manager.h"
#include "storage/table_heap.h"
#include "index/bplus_tree.h"
#include "record/schema.h"
#include "record/tuple.h"
#include "lsm/lsm_tree.h"

using namespace minidb;
using Clock = std::chrono::steady_clock;
static double Secs(Clock::time_point a, Clock::time_point b) {
  return std::chrono::duration<double>(b - a).count();
}
static long FileSize(const std::string& p) {
  struct stat st{};
  return ::stat(p.c_str(), &st) == 0 ? st.st_size : 0;
}

struct Result {
  double write_tput;   // ops/sec
  double read_us;      // microseconds / point read
  long disk_bytes;
  long live_bytes;
};

// ---- B+ tree row store: heap holds rows, B+ tree maps key -> RID ----
static Result BenchBTree(const std::vector<int>& keys, const std::vector<int>& reads) {
  std::remove("bench_bt.db");
  DiskManager dm("bench_bt.db");
  BufferPoolManager bpm(4096, &dm);
  Schema schema({{"k", TypeId::INTEGER}, {"v", TypeId::VARCHAR}}, 0);
  TableHeap heap(&bpm, TableHeap::Create(&bpm));
  BPlusTree index(&bpm, BPlusTree::Create(&bpm));

  long live = 0;
  auto t0 = Clock::now();
  for (int k : keys) {
    std::string row = Tuple({Value::Int(k), Value::Str("val_" + std::to_string(k))}).Serialize(schema);
    live += row.size() + 8;
    RID rid = heap.InsertTuple(row);
    index.Insert(k, rid);
  }
  auto t1 = Clock::now();
  bpm.FlushAll();

  std::string bytes;
  RID rid;
  auto t2 = Clock::now();
  for (int k : reads) {
    if (index.Search(k, &rid)) heap.GetTuple(rid, &bytes);
  }
  auto t3 = Clock::now();

  Result r;
  r.write_tput = keys.size() / Secs(t0, t1);
  r.read_us = Secs(t2, t3) / reads.size() * 1e6;
  r.disk_bytes = FileSize("bench_bt.db");
  r.live_bytes = live;
  std::remove("bench_bt.db");
  return r;
}

// ---- LSM store ----
static Result BenchLSM(const std::vector<int>& keys, const std::vector<int>& reads) {
  const std::string prefix = "bench_lsm";
  for (int i = 0; i < 5000; i++) std::remove((prefix + "_" + std::to_string(i) + ".sst").c_str());
  std::remove((prefix + ".manifest").c_str());

  LSMTree lsm(prefix, /*memtable_limit=*/50000, /*compaction_trigger=*/4);
  auto t0 = Clock::now();
  for (int k : keys) lsm.Put(k, "val_" + std::to_string(k));
  auto t1 = Clock::now();
  lsm.Flush();

  std::string val;
  auto t2 = Clock::now();
  for (int k : reads) lsm.Get(k, &val);
  auto t3 = Clock::now();

  Result r;
  r.write_tput = keys.size() / Secs(t0, t1);
  r.read_us = Secs(t2, t3) / reads.size() * 1e6;
  r.disk_bytes = lsm.DiskBytes();
  r.live_bytes = lsm.LiveBytes();

  for (int i = 0; i < 5000; i++) std::remove((prefix + "_" + std::to_string(i) + ".sst").c_str());
  std::remove((prefix + ".manifest").c_str());
  return r;
}

static void RunWorkload(const std::string& name, std::vector<int> keys) {
  std::mt19937 rng(12345);
  std::vector<int> reads;
  reads.reserve(keys.size());
  std::uniform_int_distribution<int> pick(0, static_cast<int>(keys.size()) - 1);
  for (size_t i = 0; i < keys.size(); i++) reads.push_back(keys[pick(rng)]);

  Result bt = BenchBTree(keys, reads);
  Result ls = BenchLSM(keys, reads);

  std::printf("\n## %s  (N = %zu)\n", name.c_str(), keys.size());
  std::printf("%-16s | %14s | %14s\n", "metric", "B+tree store", "LSM store");
  std::printf("%-16s-+-%14s-+-%14s\n", "----------------", "--------------", "--------------");
  std::printf("%-16s | %12.0f/s | %12.0f/s\n", "write throughput", bt.write_tput, ls.write_tput);
  std::printf("%-16s | %12.3f us | %12.3f us\n", "read latency", bt.read_us, ls.read_us);
  std::printf("%-16s | %14ld | %14ld\n", "disk bytes", bt.disk_bytes, ls.disk_bytes);
  std::printf("%-16s | %13.2fx | %13.2fx\n", "space amp",
              (double)bt.disk_bytes / bt.live_bytes, (double)ls.disk_bytes / ls.live_bytes);
}

int main(int argc, char** argv) {
  int N = (argc > 1) ? std::atoi(argv[1]) : 200000;

  std::vector<int> seq(N);
  for (int i = 0; i < N; i++) seq[i] = i;

  std::vector<int> rnd = seq;
  std::shuffle(rnd.begin(), rnd.end(), std::mt19937(987));

  std::cout << "MiniDB Track C benchmark: LSM vs B+ tree row store\n";
  RunWorkload("Sequential-key inserts", seq);
  RunWorkload("Random-key inserts", rnd);
  return 0;
}
