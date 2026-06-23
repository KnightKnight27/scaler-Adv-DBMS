// bench.cpp — small RocksDB benchmark for System Design Discussion, Topic 4.
//
//   Roll Number: 24BCS10183
//   Name:        Aman Yadav
//   Class:       B (2nd Year)
//
// Reproduces the numbers in this folder's README:
//   - fillrandom write throughput (sequential WAL + memtable inserts)
//   - readrandom point-lookup throughput after flush + compaction
//   - bloom-filter false-positive rate over keys known to be absent
//   - leveled vs universal compaction write/space amplification
//
// Build + run via ./run.sh (it passes the args below). Direct usage:
//   bench <db_path> <mode> <n_keys> <style>
//     mode  : all | fillrandom | readrandom
//     style : leveled | universal
//
// Tested against RocksDB 9.x installed with `brew install rocksdb`.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <random>
#include <string>
#include <vector>

#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/filter_policy.h>
#include <rocksdb/table.h>
#include <rocksdb/cache.h>
#include <rocksdb/statistics.h>

using rocksdb::DB;
using rocksdb::Options;
using rocksdb::ReadOptions;
using rocksdb::WriteOptions;
using rocksdb::Slice;
using rocksdb::Status;

namespace {

constexpr int kKeyLen = 16;    // fixed-width zero-padded numeric key
constexpr int kValLen = 100;   // 100-byte value

using Clock = std::chrono::steady_clock;

double seconds_since(Clock::time_point t0) {
  return std::chrono::duration<double>(Clock::now() - t0).count();
}

// Deterministic key so runs are reproducible: "key-0000000000000042".
std::string make_key(uint64_t i) {
  char buf[kKeyLen + 8];
  std::snprintf(buf, sizeof(buf), "key-%012llu", static_cast<unsigned long long>(i));
  return std::string(buf);
}

std::string make_value(std::mt19937_64& rng) {
  static const char alphabet[] =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  std::string v(kValLen, 'x');
  for (int j = 0; j < kValLen; ++j) v[j] = alphabet[rng() % (sizeof(alphabet) - 1)];
  return v;
}

Options make_options(const std::string& style,
                     std::shared_ptr<rocksdb::Statistics> stats) {
  Options o;
  o.create_if_missing = true;
  o.write_buffer_size = 64ULL << 20;   // 64 MB memtable
  o.max_write_buffer_number = 3;
  o.statistics = stats;

  if (style == "universal") {
    o.compaction_style = rocksdb::kCompactionStyleUniversal;
  } else {
    o.compaction_style = rocksdb::kCompactionStyleLevel;
  }

  rocksdb::BlockBasedTableOptions bbto;
  bbto.filter_policy.reset(rocksdb::NewBloomFilterPolicy(10, /*use_block_based=*/false));
  bbto.block_cache = rocksdb::NewLRUCache(32ULL << 20);  // 32 MB block cache
  bbto.cache_index_and_filter_blocks = true;
  o.table_factory.reset(rocksdb::NewBlockBasedTableFactory(bbto));
  return o;
}

void fillrandom(DB* db, uint64_t n) {
  std::mt19937_64 rng(/*seed=*/12345);
  WriteOptions wo;  // default: WAL on
  auto t0 = Clock::now();
  for (uint64_t i = 0; i < n; ++i) {
    Status s = db->Put(wo, make_key(i), make_value(rng));
    if (!s.ok()) { std::fprintf(stderr, "Put failed: %s\n", s.ToString().c_str()); return; }
  }
  double secs = seconds_since(t0);
  double bytes = static_cast<double>(n) * (kKeyLen + kValLen);
  std::printf("[fillrandom]   %llu keys, %d-byte key / %d-byte value\n",
              (unsigned long long)n, kKeyLen, kValLen);
  std::printf("  ops/sec ........ %.0f\n", n / secs);
  std::printf("  throughput ..... %.1f MB/s  (sequential WAL + memtable inserts)\n",
              bytes / secs / (1024 * 1024));
  std::printf("  elapsed ........ %.3f s\n\n", secs);
}

void readrandom(DB* db, uint64_t n, std::shared_ptr<rocksdb::Statistics> stats) {
  std::printf("  ... triggering manual flush + compaction ...\n\n");
  db->Flush(rocksdb::FlushOptions());
  db->CompactRange(rocksdb::CompactRangeOptions(), nullptr, nullptr);

  uint64_t hit0 = stats->getTickerCount(rocksdb::BLOCK_CACHE_HIT);
  uint64_t miss0 = stats->getTickerCount(rocksdb::BLOCK_CACHE_MISS);

  std::mt19937_64 rng(/*seed=*/999);
  ReadOptions ro;
  std::string out;
  auto t0 = Clock::now();
  for (uint64_t i = 0; i < n; ++i) {
    uint64_t k = rng() % n;                 // every key exists
    db->Get(ro, make_key(k), &out);
  }
  double secs = seconds_since(t0);
  uint64_t hit = stats->getTickerCount(rocksdb::BLOCK_CACHE_HIT) - hit0;
  uint64_t miss = stats->getTickerCount(rocksdb::BLOCK_CACHE_MISS) - miss0;
  double ratio = (hit + miss) ? double(hit) / double(hit + miss) : 0.0;

  std::printf("[readrandom]   %llu point lookups (post flush + compaction)\n",
              (unsigned long long)n);
  std::printf("  ops/sec ........ %.0f\n", n / secs);
  std::printf("  avg latency .... %.2f us\n", secs / n * 1e6);
  std::printf("  block cache .... hit ratio %.2f\n\n", ratio);
}

void bloomprobe(DB* db, uint64_t n, std::shared_ptr<rocksdb::Statistics> stats) {
  // Probe keys that were never inserted (offset by n) so every Get must miss.
  // A bloom false positive is a probe the filter let through to a real block
  // read even though the key is absent.
  uint64_t useful0 = stats->getTickerCount(rocksdb::BLOOM_FILTER_USEFUL);
  std::mt19937_64 rng(/*seed=*/4242);
  ReadOptions ro;
  std::string out;
  uint64_t found = 0;
  for (uint64_t i = 0; i < n; ++i) {
    uint64_t k = n + (rng() % n);           // guaranteed absent
    Status s = db->Get(ro, make_key(k), &out);
    if (s.ok()) ++found;                    // should be 0 — keys truly absent
  }
  uint64_t useful = stats->getTickerCount(rocksdb::BLOOM_FILTER_USEFUL) - useful0;
  uint64_t fp = (n > useful) ? (n - useful) : 0;
  std::printf("[bloom filter] probing %llu keys known to be ABSENT\n",
              (unsigned long long)n);
  std::printf("  configured ..... 10 bits/key\n");
  std::printf("  false positives  %llu / %llu\n",
              (unsigned long long)fp, (unsigned long long)n);
  std::printf("  measured FP rate  %.2f %%      (theory ~1.0 %%)\n",
              100.0 * double(fp) / double(n));
  if (found) std::printf("  (warning: %llu unexpected hits)\n", (unsigned long long)found);
  std::printf("\n");
}

void print_levels(DB* db) {
  std::string levels;
  db->GetProperty("rocksdb.levelstats", &levels);
  std::string total_sst;
  db->GetProperty("rocksdb.total-sst-files-size", &total_sst);
  std::printf("[on disk]      level stats follow; total-sst-files-size = %s bytes\n",
              total_sst.c_str());
  std::printf("%s\n", levels.c_str());
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <db_path> [mode] [n_keys] [style]\n", argv[0]);
    return 2;
  }
  std::string path = argv[1];
  std::string mode = (argc > 2) ? argv[2] : "all";
  uint64_t n = (argc > 3) ? std::strtoull(argv[3], nullptr, 10) : 200000;
  std::string style = (argc > 4) ? argv[4] : "leveled";

  auto stats = rocksdb::CreateDBStatistics();
  Options opts = make_options(style, stats);

  std::printf("RocksDB version : 9.x  (brew install rocksdb)\n");
  std::printf("compaction      : %s%s\n", style.c_str(),
              style == "leveled" ? " (default)" : "");
  std::printf("write_buffer    : 64 MB    bloom filter : 10 bits/key\n\n");

  DB* db = nullptr;
  Status s = DB::Open(opts, path, &db);
  if (!s.ok()) { std::fprintf(stderr, "open failed: %s\n", s.ToString().c_str()); return 1; }

  if (mode == "all" || mode == "fillrandom") fillrandom(db, n);
  if (mode == "all" || mode == "readrandom") readrandom(db, n, stats);
  if (mode == "all") bloomprobe(db, n, stats);
  if (mode == "all") print_levels(db);

  delete db;
  return 0;
}
