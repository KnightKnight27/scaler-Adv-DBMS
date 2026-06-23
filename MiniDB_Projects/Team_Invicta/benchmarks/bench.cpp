// Benchmark: LSM row store vs B+ tree heap row store (Track C requirement).
// Measures write throughput, point-read latency (hit + miss), full-scan time,
// and on-disk storage amplification, including the effect of LSM compaction.
//
//   make bench            # default 20k rows (a few seconds)
//   ./bench <num_rows>    # scale up to study compaction cost at larger N
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <random>
#include <string>
#include <vector>
#include "storage/buffer_pool_manager.h"
#include "storage/disk_manager.h"
#include "storage/heap_row_store.h"
#include "storage/lsm_row_store.h"

namespace fs = std::filesystem;
using namespace minidb;
using Clock = std::chrono::steady_clock;

static double Secs(Clock::time_point a, Clock::time_point b) {
  return std::chrono::duration<double>(b - a).count();
}

static Schema MakeSchema() {
  return Schema({{"id", TypeId::INTEGER, true}, {"payload", TypeId::VARCHAR, false}});
}

static Tuple MakeRow(int64_t i) {
  return Tuple({Value::Int(i), Value::Str("payload-data-for-row-" + std::to_string(i))});
}

struct Result {
  double write_tput, read_hit_us, read_miss_us, scan_s;
  size_t bytes_before, bytes_after;  // on disk, before/after deleting half (+compaction for LSM)
};

static size_t DirBytes(const std::string &dir) {
  size_t total = 0;
  for (const auto &e : fs::directory_iterator(dir))
    if (e.is_regular_file()) total += e.file_size();
  return total;
}

template <typename F>
static double TimePointReads(F &get, const std::vector<int64_t> &keys) {
  Tuple t;
  auto a = Clock::now();
  for (int64_t k : keys) get(k, &t);
  auto b = Clock::now();
  return Secs(a, b) / keys.size() * 1e6;  // microseconds per read
}

static Result RunStore(RowStore &store, const std::string &disk_path_or_dir,
                       bool is_dir, int N, std::mt19937 &rng) {
  Result r{};
  Schema schema = MakeSchema();

  // --- write throughput: sequential inserts ---
  auto w0 = Clock::now();
  for (int i = 0; i < N; ++i) store.Insert(i, MakeRow(i));
  store.Sync();
  auto w1 = Clock::now();
  r.write_tput = N / Secs(w0, w1);

  // --- point reads: hits (random existing keys) ---
  std::vector<int64_t> hit_keys(20000);
  std::uniform_int_distribution<int> d(0, N - 1);
  for (auto &k : hit_keys) k = d(rng);
  auto get = [&](int64_t k, Tuple *t) { return store.Get(k, t); };
  r.read_hit_us = TimePointReads(get, hit_keys);

  // --- point reads: misses (absent keys — exercises Bloom filters) ---
  std::vector<int64_t> miss_keys(20000);
  for (auto &k : miss_keys) k = N + d(rng);
  r.read_miss_us = TimePointReads(get, miss_keys);

  // --- full scan ---
  auto s0 = Clock::now();
  {
    auto cur = store.ScanAll();
    Tuple t;
    volatile size_t c = 0;
    while (cur->Next(&t)) ++c;
  }
  auto s1 = Clock::now();
  r.scan_s = Secs(s0, s1);

  // --- storage amplification: bytes on disk before / after deleting half ---
  store.Sync();
  r.bytes_before = is_dir ? DirBytes(disk_path_or_dir) : fs::file_size(disk_path_or_dir);
  for (int i = 0; i < N; i += 2) store.Delete(i);
  store.Sync();
  r.bytes_after = is_dir ? DirBytes(disk_path_or_dir) : fs::file_size(disk_path_or_dir);
  return r;
}

int main(int argc, char **argv) {
  int N = (argc > 1) ? std::atoi(argv[1]) : 20000;
  std::mt19937 rng(2026);
  std::printf("MiniDB benchmark: LSM vs B+ tree heap   (N = %d rows)\n\n", N);

  // --- B+ tree heap store ---
  std::string heap_file = "/tmp/minidb_bench_heap.db";
  fs::remove(heap_file);
  Result heap;
  {
    DiskManager disk(heap_file);
    // Buffer pool sized to hold the working set, so point reads measure B+ tree
    // algorithmic cost (warm cache) rather than buffer-pool thrashing.
    BufferPoolManager bpm(&disk, 8192);
    page_id_t hp = INVALID_PAGE_ID, ip = INVALID_PAGE_ID;
    HeapRowStore store(&bpm, MakeSchema(), &hp, &ip);
    heap = RunStore(store, heap_file, /*is_dir=*/false, N, rng);
    bpm.FlushAll();
    heap.bytes_after = fs::file_size(heap_file);
  }

  // --- LSM store ---
  std::string lsm_dir = "/tmp/minidb_bench_lsm";
  fs::remove_all(lsm_dir);
  Result lsm;
  {
    // Small (256 KB) MemTable so SSTables accumulate and compaction runs,
    // exercising the real LSM read/write/space trade-off rather than keeping
    // everything in one in-memory table.
    LSMRowStore store(lsm_dir, MakeSchema(), 256u << 10, 4);
    lsm = RunStore(store, lsm_dir, /*is_dir=*/true, N, rng);
  }

  auto row = [](const char *metric, double h, double l, const char *unit) {
    std::printf("  %-26s  %12.2f  %12.2f   %s\n", metric, h, l, unit);
  };
  std::printf("  %-26s  %12s  %12s\n", "metric", "B+Tree heap", "LSM");
  std::printf("  %s\n", std::string(66, '-').c_str());
  row("write throughput", heap.write_tput, lsm.write_tput, "rows/sec (higher better)");
  row("point read (hit)", heap.read_hit_us, lsm.read_hit_us, "us/read (lower better)");
  row("point read (miss)", heap.read_miss_us, lsm.read_miss_us, "us/read (lower better)");
  row("full scan", heap.scan_s, lsm.scan_s, "sec (lower better)");
  row("disk bytes (full)", (double)heap.bytes_before, (double)lsm.bytes_before, "bytes");
  row("disk bytes (after del half)", (double)heap.bytes_after, (double)lsm.bytes_after, "bytes");
  std::printf("\nNote: heap uses write-through page I/O; LSM buffers writes in the MemTable\n"
              "and flushes sequentially. LSM compaction reclaims space from deleted keys.\n");
  return 0;
}
