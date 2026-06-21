// RocksDB LSM-tree experiment.
// Runs the SAME write-heavy workload under two compaction strategies
// (Leveled vs Universal) and reports write / read / space amplification,
// LSM level structure, and Bloom-filter effectiveness from the real engine.
#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/statistics.h>
#include <rocksdb/table.h>
#include <rocksdb/filter_policy.h>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#include <memory>

using namespace rocksdb;

// Simple deterministic xorshift PRNG (no time/random seeding -> reproducible).
struct Rng { uint64_t s; uint64_t next(){ s^=s<<13; s^=s>>7; s^=s<<17; return s; } };

static std::string keyOf(uint64_t k){ char b[17]; snprintf(b,sizeof b,"key%013llu",(unsigned long long)k); return std::string(b); }

struct Metrics { std::string label; double wamp; uint64_t sst_bytes; uint64_t live_bytes;
                 uint64_t compact_read, compact_write, flush_write; int levels_with_files; };

static void runWorkload(const std::string& path, CompactionStyle style,
                        const std::string& label, FILE* out,
                        uint64_t NUM_ENTRIES, uint64_t KEYSPACE, int VALUE_SIZE) {
  DestroyDB(path, Options());
  Options options;
  options.create_if_missing = true;
  options.compaction_style = style;
  options.write_buffer_size = 4 << 20;                 // 4 MB memtable -> frequent flushes
  options.max_bytes_for_level_base = 16 << 20;         // small so multiple levels form
  options.level0_file_num_compaction_trigger = 4;
  options.max_write_buffer_number = 3;
  // Compression disabled so disk bytes == logical bytes: this isolates the
  // amplification caused purely by the LSM compaction strategy.
  options.compression = kNoCompression;
  auto stats = CreateDBStatistics();
  options.statistics = stats;

  BlockBasedTableOptions topt;
  topt.filter_policy.reset(NewBloomFilterPolicy(10, false));   // 10 bits/key
  topt.whole_key_filtering = true;
  options.table_factory.reset(NewBlockBasedTableFactory(topt));

  std::unique_ptr<DB> db;
  Status s = DB::Open(options, path, &db);
  if (!s.ok()) { fprintf(out, "OPEN FAILED (%s): %s\n", label.c_str(), s.ToString().c_str()); return; }

  std::string value(VALUE_SIZE, 'v');
  Rng rng{0x9E3779B97F4A7C15ull};
  WriteOptions wo;  // default: WAL on
  for (uint64_t i = 0; i < NUM_ENTRIES; ++i) {
    uint64_t k = rng.next() % KEYSPACE;                // random -> overwrites -> compaction
    // Make each value incompressible/unique so block compression can't hide I/O.
    for (int b = 0; b < VALUE_SIZE; b += 8) {
      uint64_t r = rng.next();
      for (int j = 0; j < 8 && b + j < VALUE_SIZE; ++j) value[b + j] = char(r >> (8 * j));
    }
    db->Put(wo, keyOf(k), value);
  }
  FlushOptions fo; fo.wait = true; db->Flush(fo);
  // Wait for the engine's OWN background compactions to settle (do NOT force a
  // full manual compaction — that would erase the strategy's natural LSM shape
  // and hide the space-amplification difference between leveled and universal).
  WaitForCompactOptions wfc; wfc.flush = true; db->WaitForCompact(wfc);

  // ---- read path: probe present + absent keys to exercise Bloom filters ----
  std::string got;
  uint64_t found = 0, missing = 0;
  Rng rr{0xD1B54A32D192ED03ull};
  for (int i = 0; i < 100000; ++i) {
    uint64_t k = rr.next() % (KEYSPACE * 2);           // half the space is absent
    Status g = db->Get(ReadOptions(), keyOf(k), &got);
    if (g.ok()) ++found; else ++missing;
  }

  Metrics m; m.label = label;
  std::string statsStr;  db->GetProperty("rocksdb.stats", &statsStr);
  std::string levelStats; db->GetProperty("rocksdb.levelstats", &levelStats);
  uint64_t sstBytes = 0, liveBytes = 0;
  db->GetIntProperty("rocksdb.total-sst-files-size", &sstBytes);
  db->GetIntProperty("rocksdb.estimate-live-data-size", &liveBytes);

  fprintf(out, "\n################################################################\n");
  fprintf(out, "# COMPACTION STRATEGY: %s\n", label.c_str());
  fprintf(out, "#   workload: %llu Put() of %d-byte values into a %llu-key space\n",
          (unsigned long long)NUM_ENTRIES, VALUE_SIZE, (unsigned long long)KEYSPACE);
  fprintf(out, "################################################################\n");
  fprintf(out, "%s\n", statsStr.c_str());
  fprintf(out, "----- rocksdb.levelstats (files / size per level) -----\n%s\n", levelStats.c_str());

  uint64_t cR = stats->getTickerCount(COMPACT_READ_BYTES);
  uint64_t cW = stats->getTickerCount(COMPACT_WRITE_BYTES);
  uint64_t fW = stats->getTickerCount(FLUSH_WRITE_BYTES);
  uint64_t bloomUseful = stats->getTickerCount(BLOOM_FILTER_USEFUL);
  uint64_t bloomFP = stats->getTickerCount(BLOOM_FILTER_FULL_POSITIVE);
  uint64_t bloomTP = stats->getTickerCount(BLOOM_FILTER_FULL_TRUE_POSITIVE);
  uint64_t bytesWritten = stats->getTickerCount(BYTES_WRITTEN);

  double userBytes = (double)NUM_ENTRIES * (VALUE_SIZE + 16);
  double wamp = userBytes > 0 ? (double)(cW + fW) / userBytes : 0.0;
  double spaceAmp = liveBytes > 0 ? (double)sstBytes / (double)liveBytes : 0.0;

  fprintf(out, "----- DERIVED AMPLIFICATION (%s) -----\n", label.c_str());
  fprintf(out, "user bytes Put (keys+values)     : %.1f MB\n", userBytes/1e6);
  fprintf(out, "flush bytes written (memtable->L0): %.1f MB\n", fW/1e6);
  fprintf(out, "compaction bytes written          : %.1f MB\n", cW/1e6);
  fprintf(out, "compaction bytes read             : %.1f MB\n", cR/1e6);
  fprintf(out, "WRITE AMPLIFICATION (flush+compact)/user = %.2fx\n", wamp);
  fprintf(out, "total SST size on disk            : %.1f MB\n", sstBytes/1e6);
  fprintf(out, "estimated live data size          : %.1f MB\n", liveBytes/1e6);
  fprintf(out, "SPACE AMPLIFICATION  sst/live      = %.2fx\n", spaceAmp);
  fprintf(out, "----- READ PATH / BLOOM FILTER (%s) -----\n", label.c_str());
  fprintf(out, "point Get() probes: %llu found, %llu missing\n",
          (unsigned long long)found, (unsigned long long)missing);
  fprintf(out, "BLOOM_FILTER_USEFUL (SST reads skipped)   : %llu\n", (unsigned long long)bloomUseful);
  fprintf(out, "BLOOM_FILTER_FULL_POSITIVE (passed filter): %llu\n", (unsigned long long)bloomFP);
  fprintf(out, "BLOOM_FILTER_FULL_TRUE_POSITIVE (real hit): %llu\n", (unsigned long long)bloomTP);
  if (bloomFP) fprintf(out, "bloom false-positive rate ~ (FP-TP)/FP = %.4f\n",
                       (double)(bloomFP - bloomTP)/(double)bloomFP);

}

int main() {
  FILE* out = fopen("rocksdb_experiments.txt", "w");
  fprintf(out, "############################################################\n");
  fprintf(out, "# RocksDB LSM-tree experiment (linked against librocksdb)\n");
  fprintf(out, "############################################################\n");

  const uint64_t NUM_ENTRIES = 2000000;   // 2M Puts
  const uint64_t KEYSPACE    = 1000000;   // 1M distinct keys -> ~2x overwrite
  const int      VALUE_SIZE  = 100;

  runWorkload("rocks_leveled",   kCompactionStyleLevel,     "LEVELED compaction",   out, NUM_ENTRIES, KEYSPACE, VALUE_SIZE);
  runWorkload("rocks_universal", kCompactionStyleUniversal, "UNIVERSAL compaction", out, NUM_ENTRIES, KEYSPACE, VALUE_SIZE);

  fprintf(out, "\nDONE\n");
  fclose(out);
  printf("rocksdb experiment complete\n");
  return 0;
}
