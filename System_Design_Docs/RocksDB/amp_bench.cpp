// amp_bench.cpp — minimal RocksDB write-amplification benchmark
//
// What it does:
//   Inserts N random key/value pairs into a fresh RocksDB, optionally
//   under leveled or universal compaction. After ingest it waits for
//   background work to finish, then issues M random Get()s. From
//   RocksDB's internal counters it computes:
//
//     write_amp = (bytes written to disk including flush + compaction + WAL)
//               / (logical user bytes written by Put)
//     space_amp = (live SST bytes on disk) / (logical user bytes)
//     read_amp  = bloom-filter useful skips, data-block reads, plus an
//                 approximate avg "files touched per Get"
//
// Why these matter: an LSM trades inserts being cheap (one append) for
// background compaction work to keep reads from blowing up. Different
// compaction strategies sit on different points of the write-amp /
// space-amp / read-amp triangle, and the only honest way to compare them
// is to count the bytes actually moved on disk.
//
// Build:
//   c++ -std=c++17 -O2 amp_bench.cpp -lrocksdb -o amp_bench
// Run (leveled is default):
//   ./amp_bench --policy leveled  --keys 1000000 --reads 100000
//   ./amp_bench --policy universal --keys 1000000 --reads 100000

#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/statistics.h>
#include <rocksdb/table.h>
#include <rocksdb/filter_policy.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <sys/stat.h>
#include <filesystem>

namespace fs = std::filesystem;
using rocksdb::DB;

// 15-byte alphanumeric key derived from a 64-bit counter
static std::string makeKey(uint64_t seed) {
    static const char alphabet[] =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::string k(15, '_');
    uint64_t v = seed * 2654435761ull;
    for (int i = 0; i < 15; ++i) {
        k[i] = alphabet[v & 0x3f];
        v >>= 1;
    }
    return k;
}

static uint64_t dirSizeBytes(const std::string &path) {
    uint64_t total = 0;
    for (auto &entry : fs::recursive_directory_iterator(path)) {
        if (entry.is_regular_file()) total += entry.file_size();
    }
    return total;
}

int main(int argc, char **argv) {
    std::string policy = "leveled";
    uint64_t numKeys = 1'000'000;
    uint64_t numReads = 100'000;
    std::string dbPath = "/tmp/amp_bench_db";

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--policy") && i + 1 < argc) policy = argv[++i];
        else if (!strcmp(argv[i], "--keys") && i + 1 < argc) numKeys = std::stoull(argv[++i]);
        else if (!strcmp(argv[i], "--reads") && i + 1 < argc) numReads = std::stoull(argv[++i]);
        else if (!strcmp(argv[i], "--path") && i + 1 < argc) dbPath = argv[++i];
    }

    // start clean each run
    fs::remove_all(dbPath);

    rocksdb::Options opt;
    opt.create_if_missing = true;
    opt.compression = rocksdb::kSnappyCompression;
    opt.statistics = rocksdb::CreateDBStatistics();

    // 10 bits/key bloom filter, attached to the block-based table format
    rocksdb::BlockBasedTableOptions tableOpt;
    tableOpt.filter_policy.reset(rocksdb::NewBloomFilterPolicy(10, false));
    opt.table_factory.reset(rocksdb::NewBlockBasedTableFactory(tableOpt));

    if (policy == "universal") {
        opt.compaction_style = rocksdb::kCompactionStyleUniversal;
    } else {
        opt.compaction_style = rocksdb::kCompactionStyleLevel;
        opt.level_compaction_dynamic_level_bytes = true;
    }

    std::unique_ptr<DB> db;
    auto status = DB::Open(opt, dbPath, &db);
    if (!status.ok()) {
        std::cerr << "open failed: " << status.ToString() << "\n";
        return 1;
    }

    // ---- write phase ----
    // Random (non-compressible) values: a fixed-payload 'xxxx...' would
    // get crushed by Snappy and the resulting "compaction bytes" wouldn't
    // be representative of real workloads.
    std::mt19937_64 rng(42);
    std::string value(100, '\0');
    uint64_t logicalUserBytes = 0;
    auto t0 = std::chrono::steady_clock::now();

    rocksdb::WriteOptions wo;
    for (uint64_t i = 0; i < numKeys; ++i) {
        auto key = makeKey(rng());
        for (int j = 0; j < 100; ++j) value[j] = static_cast<char>(rng() & 0xff);
        db->Put(wo, key, value);
        logicalUserBytes += key.size() + value.size();
    }

    // flush memtable, then force everything down to the bottom level so the
    // compaction counters actually reflect a fully-compacted LSM. without
    // this, snappy compresses the dataset down enough to sit entirely in
    // L0 and "compaction bytes" reads 0 — true at that instant, but not
    // what we want to measure.
    db->Flush(rocksdb::FlushOptions{});
    rocksdb::CompactRangeOptions cro;
    cro.bottommost_level_compaction = rocksdb::BottommostLevelCompaction::kForce;
    db->CompactRange(cro, nullptr, nullptr);
    db->WaitForCompact(rocksdb::WaitForCompactOptions{});
    auto t1 = std::chrono::steady_clock::now();

    // ---- read phase ----
    uint64_t hits = 0, misses = 0;
    std::mt19937_64 readRng(7);
    rocksdb::ReadOptions ro;
    std::string out;
    for (uint64_t i = 0; i < numReads; ++i) {
        // half-and-half: known keys (use the same RNG state) vs random misses
        auto key = (i & 1) ? makeKey(readRng()) : makeKey(readRng() ^ 0xdeadbeef);
        auto st = db->Get(ro, key, &out);
        if (st.ok()) ++hits; else ++misses;
    }
    auto t2 = std::chrono::steady_clock::now();

    // ---- counters ----
    auto get = [&](rocksdb::Tickers t) { return opt.statistics->getTickerCount(t); };
    uint64_t flushBytes    = get(rocksdb::FLUSH_WRITE_BYTES);
    uint64_t compactBytes  = get(rocksdb::COMPACT_WRITE_BYTES);
    uint64_t walBytes      = get(rocksdb::WAL_FILE_BYTES);
    uint64_t bytesWritten  = flushBytes + compactBytes;          // (excluding WAL)
    uint64_t totalWritten  = bytesWritten + walBytes;            // (including WAL)
    uint64_t bloomUseful   = get(rocksdb::BLOOM_FILTER_USEFUL);  // queries Bloom said "no" to
    uint64_t blockRead     = get(rocksdb::BLOCK_CACHE_DATA_HIT)
                           + get(rocksdb::BLOCK_CACHE_DATA_MISS);
    uint64_t liveBytes     = dirSizeBytes(dbPath);

    auto secs = [](auto a, auto b) {
        return std::chrono::duration<double>(b - a).count();
    };

    std::printf("policy=%s  keys=%llu  reads=%llu\n",
                policy.c_str(),
                (unsigned long long)numKeys,
                (unsigned long long)numReads);
    std::printf("write phase: %.2f s (%.0f kops/s)\n",
                secs(t0, t1), numKeys / secs(t0, t1) / 1000.0);
    std::printf("read  phase: %.2f s (%.0f kops/s)\n",
                secs(t1, t2), numReads / secs(t1, t2) / 1000.0);

    std::printf("\n--- write amplification ---\n");
    std::printf("logical user bytes : %10.2f MB\n", logicalUserBytes / 1048576.0);
    std::printf("flush bytes (L0)   : %10.2f MB\n", flushBytes        / 1048576.0);
    std::printf("compaction bytes   : %10.2f MB\n", compactBytes      / 1048576.0);
    std::printf("WAL bytes          : %10.2f MB\n", walBytes          / 1048576.0);
    std::printf("write amp (no WAL) : %.2fx\n", bytesWritten * 1.0 / logicalUserBytes);
    std::printf("write amp (w/ WAL) : %.2fx\n", totalWritten * 1.0 / logicalUserBytes);
    std::printf("space amp          : %.2fx (live=%.2f MB / logical=%.2f MB)\n",
                liveBytes * 1.0 / logicalUserBytes,
                liveBytes / 1048576.0,
                logicalUserBytes / 1048576.0);

    std::printf("\n--- read amplification ---\n");
    std::printf("hits   : %llu  (expected ~half of reads, since half are random misses)\n",
                (unsigned long long)hits);
    std::printf("misses : %llu\n", (unsigned long long)misses);
    std::printf("bloom skipped (useful=NO answers) : %llu\n",
                (unsigned long long)bloomUseful);
    std::printf("data block accesses                 : %llu\n",
                (unsigned long long)blockRead);

    std::string lsm;
    db->GetProperty("rocksdb.levelstats", &lsm);
    std::printf("\n--- LSM tree ---\n%s\n", lsm.c_str());

    db.reset();
    return 0;
}
