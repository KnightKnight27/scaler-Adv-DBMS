// lsm_bench.cpp
// A small, self-contained RocksDB experiment that demonstrates:
//   1. The LSM write path (MemTable -> flush -> SSTs across levels L0..Ln)
//   2. Write amplification (bytes written to disk vs logical bytes)
//   3. Space amplification before/after compaction
//   4. The effect of a Bloom filter on point lookups of ABSENT keys
//      (probing absent keys that lie INSIDE the stored key range, so the
//       per-file min/max check cannot short-circuit and the filter is exercised)
//
// Build:
//   clang++ -std=c++20 lsm_bench.cpp -o lsm_bench \
//     -I/opt/homebrew/opt/rocksdb/include -L/opt/homebrew/opt/rocksdb/lib -lrocksdb
//
// Run: ./lsm_bench [num_keys]   (prints real numbers, not estimates)

#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/statistics.h>
#include <rocksdb/table.h>
#include <rocksdb/filter_policy.h>
#include <cstdio>
#include <chrono>
#include <string>
#include <vector>
#include <memory>

using namespace rocksdb;
using Clock = std::chrono::steady_clock;

static std::string key_of(long long i) {
    char buf[32];
    snprintf(buf, sizeof(buf), "key%012lld", i); // zero-padded: lexical order == numeric order
    return std::string(buf);
}
static double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// Build a DB, bulk-load EVEN keys, and return it plus its statistics handle.
static std::unique_ptr<DB> build_and_load(const std::string& dir, bool bloom,
                                          long long N, int VALSZ,
                                          std::shared_ptr<Statistics>& stats_out,
                                          double& write_ms_out) {
    system(("rm -rf " + dir).c_str());
    Options options;
    options.create_if_missing = true;
    options.compression = kNoCompression;          // isolate amplification from compression
    options.statistics = CreateDBStatistics();
    options.write_buffer_size = 8 * 1024 * 1024;   // small memtable -> many flushes -> real LSM shape
    options.max_bytes_for_level_base = 32 * 1024 * 1024;
    options.level0_file_num_compaction_trigger = 4;

    BlockBasedTableOptions bbto;
    if (bloom) {
        bbto.filter_policy.reset(NewBloomFilterPolicy(10, false)); // 10 bits/key
        bbto.whole_key_filtering = true;
    } else {
        bbto.filter_policy.reset(); // no filter
    }
    // Force lookups to actually touch SST files rather than a warm block cache.
    bbto.no_block_cache = true;
    options.table_factory.reset(NewBlockBasedTableFactory(bbto));

    std::unique_ptr<DB> db;
    Status s = DB::Open(options, dir, &db);
    if (!s.ok()) { fprintf(stderr, "open failed: %s\n", s.ToString().c_str()); exit(1); }

    std::string val(VALSZ, 'x');
    WriteOptions wo;
    auto t0 = Clock::now();
    // Insert EVEN keys in RANDOM order so memtable flushes overlap and real
    // (non-trivial) compaction happens. Odd keys stay absent for the bloom test.
    long long state = 88172645463325252LL;
    for (long long i = 0; i < N; ++i) {
        state = state * 6364136223846793005LL + 1442695040888963407LL;
        long long r = ((state >> 17) & 0x7fffffffffffffffLL) % N;
        db->Put(wo, key_of(2 * r), val);
    }
    db->Flush(FlushOptions());
    write_ms_out = ms_since(t0);
    stats_out = options.statistics;
    return db;
}

// Probe ABSENT odd keys that lie inside [0, 2N): the bloom filter (if present)
// should let RocksDB skip the SST data blocks.
static double probe_absent(DB* db, long long PROBES, long long N, uint64_t& found) {
    std::string got;
    auto t0 = Clock::now();
    found = 0;
    for (long long i = 0; i < PROBES; ++i) {
        long long oddKey = 2 * (i % N) + 1; // guaranteed absent, inside key range
        if (db->Get(ReadOptions(), key_of(oddKey), &got).ok()) found++;
    }
    return ms_since(t0);
}

int main(int argc, char** argv) {
    const long long N = (argc > 1) ? atoll(argv[1]) : 2000000;
    const int VALSZ = 100;
    const long long PROBES = 200000;
    const std::string base = "/tmp/rocks_lsm";

    printf("############ RocksDB LSM experiment (v11.1.1) ############\n");
    printf("stored keys=%lld (even only) value=%dB memtable=8MB compression=none\n\n", N, VALSZ);

    // ---------- DB with Bloom filter ----------
    std::shared_ptr<Statistics> S;
    double write_ms = 0;
    std::unique_ptr<DB> db = build_and_load(base + "_bloom", /*bloom=*/true, N, VALSZ, S, write_ms);
    long long logical_bytes = N * (long long)(VALSZ + 15);

    printf("===== 1. Write path =====\n");
    printf("wrote %lld puts in %.0f ms  (%.0f K ops/sec)\n", N, write_ms, N / write_ms);

    std::string levelstats;
    db->GetProperty("rocksdb.levelstats", &levelstats);
    printf("\n===== 2. LSM level structure (files + size per level) =====\n%s\n", levelstats.c_str());

    uint64_t flush_bytes   = S->getTickerCount(FLUSH_WRITE_BYTES);
    uint64_t compact_write = S->getTickerCount(COMPACT_WRITE_BYTES);
    uint64_t compact_read  = S->getTickerCount(COMPACT_READ_BYTES);
    double w_amp = (double)(flush_bytes + compact_write) / (double)logical_bytes;
    printf("===== 3. Write amplification =====\n");
    printf("logical user bytes:        %.1f MB\n", logical_bytes / 1e6);
    printf("flushed to L0 (SST):       %.1f MB\n", flush_bytes / 1e6);
    printf("written by compaction:     %.1f MB\n", compact_write / 1e6);
    printf("read by compaction:        %.1f MB\n", compact_read / 1e6);
    printf("=> write amplification:    %.2fx\n\n", w_amp);

    uint64_t live_b = 0, all_b = 0;
    db->GetIntProperty("rocksdb.estimate-live-data-size", &live_b);
    db->GetIntProperty("rocksdb.total-sst-files-size", &all_b);
    printf("===== 4. Space amplification =====\n");
    printf("before full compaction: live=%.1f MB  on-disk=%.1f MB  space-amp=%.2fx\n",
           live_b/1e6, all_b/1e6, (double)all_b/(double)live_b);
    db->CompactRange(CompactRangeOptions(), nullptr, nullptr);
    uint64_t live_a = 0, all_a = 0;
    db->GetIntProperty("rocksdb.estimate-live-data-size", &live_a);
    db->GetIntProperty("rocksdb.total-sst-files-size", &all_a);
    printf("after  full compaction: live=%.1f MB  on-disk=%.1f MB  space-amp=%.2fx\n\n",
           live_a/1e6, all_a/1e6, (double)all_a/(double)live_a);

    // ---- Bloom A/B on absent-in-range keys ----
    S->getAndResetTickerCount(BLOOM_FILTER_USEFUL);
    uint64_t found = 0;
    double bloom_ms = probe_absent(db.get(), PROBES, N, found);
    uint64_t bloom_useful = S->getTickerCount(BLOOM_FILTER_USEFUL);

    std::shared_ptr<Statistics> S2;
    double wm2 = 0;
    std::unique_ptr<DB> db2 = build_and_load(base + "_nobloom", /*bloom=*/false, N, VALSZ, S2, wm2);
    db2->CompactRange(CompactRangeOptions(), nullptr, nullptr);
    uint64_t found2 = 0;
    double nobloom_ms = probe_absent(db2.get(), PROBES, N, found2);

    printf("===== 5. Bloom filter effect: %lld ABSENT lookups (keys inside stored range) =====\n", PROBES);
    printf("with bloom filter:    %.0f ms  (%.2f us/op)  bloom_useful(SST blocks skipped)=%llu  found=%llu\n",
           bloom_ms, bloom_ms*1000/PROBES, (unsigned long long)bloom_useful, (unsigned long long)found);
    printf("without bloom filter: %.0f ms  (%.2f us/op)  found=%llu\n",
           nobloom_ms, nobloom_ms*1000/PROBES, (unsigned long long)found2);
    printf("=> speedup from bloom filter on negative lookups: %.1fx\n\n", nobloom_ms / bloom_ms);

    printf("===== 6. Compaction stats (rocksdb.stats, bloom DB) =====\n");
    std::string stats;
    db->GetProperty("rocksdb.stats", &stats);
    printf("%s\n", stats.c_str());

    db.reset();
    db2.reset();
    return 0;
}
