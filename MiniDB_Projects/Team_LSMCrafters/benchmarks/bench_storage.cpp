// Benchmarks the B+Tree/heap storage engine against the Track C LSM engine on
// the same workload, through the shared StorageEngine interface. Measures write
// throughput, point-read latency (existing and missing keys), and on-disk size
// before and after compaction. Build in Release; numbers show trends, not
// absolutes (they depend on tuning).
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <random>
#include "bench_common.h"
#include "lsm/lsm_table.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"
#include "storage/heap_table.h"

using namespace minidb;

namespace {

struct Metrics {
  double   load_ops_sec = 0, update_ops_sec = 0;
  double   read_hit_us = 0, read_miss_us = 0;
  uint64_t disk_pre = 0, disk_post = 0;
};

Metrics run_workload(const std::string& name, StorageEngine& store,
                     const std::vector<Key>& keys, std::size_t row_bytes, int read_n,
                     const std::function<void()>& flush_fn,
                     const std::function<void()>& compact_fn,
                     const std::function<uint64_t()>& disk_fn) {
  const int n = static_cast<int>(keys.size());
  Metrics m;
  Timer t;
  std::mt19937_64 rng(99);

  // 1. Load: insert all keys in random order.
  t.start();
  for (Key k : keys) store.insert(k, gen_value(k, row_bytes));
  m.load_ops_sec = n / t.seconds();
  flush_fn();
  m.disk_pre = disk_fn();

  // 2. Point reads of existing keys.
  t.start();
  for (int i = 0; i < read_n; ++i) {
    auto v = store.get(keys[rng() % n]);
    assert(v);
  }
  m.read_hit_us = t.seconds() * 1e6 / read_n;

  // 3. Point reads of missing keys.
  t.start();
  for (int i = 0; i < read_n; ++i) store.get(static_cast<Key>(n) + (rng() % n));
  m.read_miss_us = t.seconds() * 1e6 / read_n;

  // 4. Updates (overwrite existing keys).
  int updates = n / 2;
  t.start();
  for (int i = 0; i < updates; ++i) {
    Key k = keys[rng() % n];
    store.insert(k, gen_value(k, row_bytes));
  }
  m.update_ops_sec = updates / t.seconds();

  // 5. Compact, then measure space again.
  compact_fn();
  m.disk_post = disk_fn();

  std::printf("%-8s load=%8.0f ins/s  update=%8.0f ins/s  read(hit)=%6.2f us  "
              "read(miss)=%6.2f us  disk: %llu -> %llu KB\n",
              name.c_str(), m.load_ops_sec, m.update_ops_sec, m.read_hit_us, m.read_miss_us,
              static_cast<unsigned long long>(m.disk_pre / 1024),
              static_cast<unsigned long long>(m.disk_post / 1024));
  return m;
}

// Result of the per-write durability test: how fast inserts can be made durable,
// and how many bytes the engine physically wrote to do so.
struct WriteResult { double ops_sec = 0; uint64_t bytes = 0; };

// Inserts every key and makes each insert durable, then reports throughput and
// the physical bytes written per row (write amplification). This is the metric
// the LSM is designed to win: a force-policy heap must persist a whole 4 KB page
// per row, while the LSM appends only the row to its sequential WAL.
WriteResult durable_writes(const std::string& name, StorageEngine& store,
                           const std::vector<Key>& keys, std::size_t row_bytes,
                           const std::function<void()>& make_durable,
                           const std::function<uint64_t()>& bytes_fn) {
  Timer t;
  t.start();
  for (Key k : keys) {
    store.insert(k, gen_value(k, row_bytes));
    make_durable();
  }
  WriteResult r;
  r.ops_sec = keys.size() / t.seconds();
  r.bytes   = bytes_fn();
  std::printf("%-14s %9.0f durable ins/s   wrote %7llu KB  (%.0f bytes/row)\n",
              name.c_str(), r.ops_sec,
              static_cast<unsigned long long>(r.bytes / 1024),
              static_cast<double>(r.bytes) / keys.size());
  return r;
}

// Confirms both engines agree on a small workload before we trust the timings.
void cross_check() {
  std::filesystem::remove("/tmp/bench_xcheck.db");
  std::filesystem::remove_all("/tmp/bench_xcheck_lsm");
  DiskManager disk("/tmp/bench_xcheck.db");
  BufferPool pool(disk);
  auto heap = make_heap_table(pool);
  LsmTable lsm("/tmp/bench_xcheck_lsm");

  for (int i = 0; i < 200; ++i) {
    Bytes v = gen_value(i, 32);
    heap->insert(i, v);
    lsm.insert(i, v);
  }
  heap->erase(50);
  lsm.erase(50);
  for (int i = 0; i < 200; ++i) {
    assert(heap->get(i).has_value() == lsm.get(i).has_value());
    if (heap->get(i)) assert(*heap->get(i) == *lsm.get(i));
  }
  std::printf("cross-check OK: B+Tree and LSM agree on get() for 200 keys\n\n");
}

}  // namespace

int main() {
  const int         N         = 50000;
  const std::size_t ROW_BYTES = 100;
  const int         READ_N    = 20000;
  const uint64_t    SEED      = 42;
  const uint64_t    logical   = static_cast<uint64_t>(N) * (sizeof(Key) + ROW_BYTES);

  cross_check();
  std::printf("Workload: %d keys, %zu-byte rows, %d point reads. Logical data = %llu KB\n\n",
              N, ROW_BYTES, READ_N, static_cast<unsigned long long>(logical / 1024));

  std::vector<Key> keys = gen_keys(N, SEED);
  CsvWriter csv("benchmarks/results/storage_bench.csv");

  Metrics heap_m, lsm_m;

  // B+Tree / heap engine.
  {
    const std::string db = "/tmp/bench_heap.db";
    std::filesystem::remove(db);
    DiskManager disk(db);
    BufferPool pool(disk);
    auto heap = make_heap_table(pool);
    heap_m = run_workload("B+Tree", *heap, keys, ROW_BYTES, READ_N,
                          [&] { heap->flush(); }, [&] { heap->flush(); },
                          [&] { return path_size(db); });
  }

  // LSM engine.
  {
    const std::string dir = "/tmp/bench_lsm";
    std::filesystem::remove_all(dir);
    LsmOptions opt;
    opt.sync_wal = false;  // measure the engine, not fsync latency
    LsmTable lsm(dir, opt);
    lsm_m = run_workload("LSM", lsm, keys, ROW_BYTES, READ_N,
                         [&] { lsm.flush(); }, [&] { lsm.force_compact(); },
                         [&] { return path_size(dir); });
  }

  // Scenario B: per-write durability. Here the heap uses a FORCE policy (persist
  // the dirty page after every insert), the model the textbook "LSM wins writes"
  // result assumes. The LSM gets a memtable large enough that no flush/compaction
  // happens during the run, so each insert is just a MemTable put plus one
  // sequential WAL append. This isolates the LSM's actual write advantage.
  std::printf("\nPer-write durability (FORCE-policy heap vs WAL-backed LSM), %d rows:\n", N);
  WriteResult heap_w, lsm_w;
  {
    const std::string db = "/tmp/bench_heap_durable.db";
    std::filesystem::remove(db);
    DiskManager disk(db);
    BufferPool pool(disk);
    auto heap = make_heap_table(pool);
    const uint64_t base = disk.bytes_written();  // ignore the bootstrap page
    heap_w = durable_writes("heap (FORCE)", *heap, keys, ROW_BYTES,
                            [&] { heap->flush(); }, [&] { return disk.bytes_written() - base; });
  }
  {
    const std::string dir = "/tmp/bench_lsm_durable";
    std::filesystem::remove_all(dir);
    LsmOptions opt;
    opt.sync_wal           = true;       // every insert is durable via the WAL
    opt.memtable_threshold = 1u << 30;   // 1 GiB: no flush/compaction during the run
    LsmTable lsm(dir, opt);
    lsm_w = durable_writes("LSM (WAL)", lsm, keys, ROW_BYTES,
                           [] {}, [&] { return path_size(dir); });
  }

  auto record = [&](const char* e, const Metrics& m) {
    csv.row(e, "load", "ops_per_sec", m.load_ops_sec);
    csv.row(e, "update", "ops_per_sec", m.update_ops_sec);
    csv.row(e, "read_hit", "latency_us", m.read_hit_us);
    csv.row(e, "read_miss", "latency_us", m.read_miss_us);
    csv.row(e, "space", "disk_pre_bytes", static_cast<double>(m.disk_pre));
    csv.row(e, "space", "disk_post_bytes", static_cast<double>(m.disk_post));
    csv.row(e, "space", "space_amp_post", static_cast<double>(m.disk_post) / logical);
  };
  record("B+Tree", heap_m);
  record("LSM", lsm_m);

  csv.row("heap_FORCE", "durable_write", "ops_per_sec", heap_w.ops_sec);
  csv.row("heap_FORCE", "durable_write", "bytes_per_row", static_cast<double>(heap_w.bytes) / N);
  csv.row("LSM_WAL", "durable_write", "ops_per_sec", lsm_w.ops_sec);
  csv.row("LSM_WAL", "durable_write", "bytes_per_row", static_cast<double>(lsm_w.bytes) / N);

  std::printf("\nWrote benchmarks/results/storage_bench.csv\n");
  return 0;
}
