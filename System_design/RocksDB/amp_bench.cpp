// Measures write / read / space amplification on a real RocksDB instance.
// Build: see run.sh.  Links against Homebrew librocksdb.
#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/statistics.h>
#include <rocksdb/table.h>
#include <rocksdb/filter_policy.h>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#include <random>

using namespace rocksdb;

static std::string key_of(uint64_t k) {
    char buf[16];
    // big-endian-ish fixed width so keys spread across the keyspace
    snprintf(buf, sizeof(buf), "%015llu", (unsigned long long)k);
    return std::string(buf, 15);
}

static uint64_t prop(DB* db, const char* name) {
    uint64_t v = 0;
    db->GetIntProperty(name, &v);
    return v;
}

struct Result { std::string label; };

void run(const std::string& path, bool universal, uint64_t N) {
    DestroyDB(path, Options());

    Options options;
    options.create_if_missing = true;
    options.compression = kNoCompression;          // isolate amplification from compression
    options.statistics = CreateDBStatistics();
    options.write_buffer_size = 8 << 20;            // 8 MB memtable -> forces many flushes
    options.max_bytes_for_level_base = 32 << 20;
    options.target_file_size_base = 4 << 20;
    if (universal) options.compaction_style = kCompactionStyleUniversal;

    BlockBasedTableOptions topt;
    topt.filter_policy.reset(NewBloomFilterPolicy(10, false)); // 10 bits/key bloom
    options.table_factory.reset(NewBlockBasedTableFactory(topt));

    std::unique_ptr<DB> db;
    Status s = DB::Open(options, path, &db);
    if (!s.ok()) { fprintf(stderr, "open failed: %s\n", s.ToString().c_str()); return; }

    const int VAL = 100;
    std::string value(VAL, 'x');
    std::mt19937_64 rng(42);
    WriteOptions wo;

    uint64_t logical_bytes = 0;
    for (uint64_t i = 0; i < N; i++) {
        uint64_t k = rng() % (N * 2);               // random keys, some overwrite
        std::string key = key_of(k);
        db->Put(wo, key, value);
        logical_bytes += key.size() + VAL;
    }
    FlushOptions fo; fo.wait = true;
    db->Flush(fo);
    db->CompactRange(CompactRangeOptions(), nullptr, nullptr); // settle the LSM

    // ---- write amplification ----
    auto st = options.statistics;
    uint64_t flush_w  = st->getTickerCount(FLUSH_WRITE_BYTES);
    uint64_t compact_w= st->getTickerCount(COMPACT_WRITE_BYTES);
    uint64_t wal_w    = st->getTickerCount(WAL_FILE_BYTES);
    double write_amp = double(flush_w + compact_w) / double(logical_bytes ? logical_bytes : 1);

    // ---- space amplification ----
    uint64_t sst_size  = prop(db.get(), "rocksdb.total-sst-files-size");
    uint64_t live_size = prop(db.get(), "rocksdb.estimate-live-data-size");
    double space_amp = double(sst_size) / double(live_size ? live_size : 1);

    // ---- read path + bloom filter effect ----
    uint64_t hits = 0, misses = 0;
    ReadOptions ro;
    std::string out;
    for (uint64_t i = 0; i < 100000; i++) {
        uint64_t k = rng() % (N * 4);               // ~half present, half absent
        Status g = db->Get(ro, key_of(k), &out);
        if (g.ok()) hits++; else misses++;
    }
    uint64_t bloom_useful = st->getTickerCount(BLOOM_FILTER_USEFUL); // negatives bloom answered
    uint64_t bloom_full_pos = st->getTickerCount(BLOOM_FILTER_FULL_POSITIVE);
    uint64_t block_read = st->getTickerCount(BLOCK_CACHE_DATA_MISS);

    printf("\n========== %s compaction ==========\n", universal ? "UNIVERSAL" : "LEVELED");
    printf("logical user bytes written : %.1f MB\n", logical_bytes/1048576.0);
    printf("flush bytes -> L0          : %.1f MB\n", flush_w/1048576.0);
    printf("compaction bytes written   : %.1f MB\n", compact_w/1048576.0);
    printf("WAL bytes written          : %.1f MB\n", wal_w/1048576.0);
    printf("WRITE AMPLIFICATION        : %.2fx  (flush+compaction)/user\n", write_amp);
    printf("---\n");
    printf("live data (logical)        : %.1f MB\n", live_size/1048576.0);
    printf("SST files on disk          : %.1f MB\n", sst_size/1048576.0);
    printf("SPACE AMPLIFICATION        : %.2fx  sst/live\n", space_amp);
    printf("---\n");
    printf("point reads                : 100000  (hits=%llu misses=%llu)\n",
           (unsigned long long)hits, (unsigned long long)misses);
    printf("bloom filter useful (skips): %llu  full-positives: %llu\n",
           (unsigned long long)bloom_useful, (unsigned long long)bloom_full_pos);
    printf("data blocks read from disk : %llu\n", (unsigned long long)block_read);

    std::string levelstats;
    db->GetProperty("rocksdb.levelstats", &levelstats);
    printf("--- per-level (rocksdb.levelstats) ---\n%s", levelstats.c_str());
    DestroyDB(path, Options());
}

int main() {
    uint64_t N = 1000000;       // 1M operations, ~115 MB logical
    run("/tmp/rocks_leveled",  false, N);
    run("/tmp/rocks_universal", true, N);
    return 0;
}
