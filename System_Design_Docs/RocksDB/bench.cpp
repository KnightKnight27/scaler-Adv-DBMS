#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/statistics.h>
#include <rocksdb/filter_policy.h>
#include <rocksdb/table.h>
#include <chrono>
#include <iostream>
#include <random>
#include <string>
#include <vector>
#include <iomanip>

using namespace rocksdb;
using namespace std::chrono;

static std::string make_key(uint64_t i) {
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)i);
    return std::string(buf);
}

int main(int argc, char** argv) {
    std::string dbpath = argc > 1 ? argv[1] : "/tmp/rocks_bench/db";
    std::string mode   = argc > 2 ? argv[2] : "all";   // fillseq | fillrandom | readrandom | mixed
    int n              = argc > 3 ? std::stoi(argv[3]) : 200000;
    bool universal     = argc > 4 && std::string(argv[4]) == "universal";

    Options opts;
    opts.create_if_missing = true;
    opts.statistics = CreateDBStatistics();
    opts.write_buffer_size = 4 * 1024 * 1024;
    opts.max_bytes_for_level_base = 16 * 1024 * 1024;
    if (universal) opts.compaction_style = kCompactionStyleUniversal;
    BlockBasedTableOptions table_opts;
    table_opts.filter_policy.reset(NewBloomFilterPolicy(10, false));
    opts.table_factory.reset(NewBlockBasedTableFactory(table_opts));

    std::unique_ptr<DB> db;
    Status s = DB::Open(opts, dbpath, &db);
    if (!s.ok()) { std::cerr << "open: " << s.ToString() << "\n"; return 1; }

    std::mt19937_64 rng(42);
    std::string val(100, 'x');

    auto run_seq_writes = [&](int count) {
        auto t0 = steady_clock::now();
        for (int i = 0; i < count; ++i) db->Put(WriteOptions(), make_key(i), val);
        auto dt = duration_cast<microseconds>(steady_clock::now() - t0).count();
        std::cout << "fillseq:    " << count << " writes in " << dt/1000 << " ms = "
                  << std::fixed << std::setprecision(0) << (count*1e6/dt) << " ops/s\n";
    };
    auto run_rand_writes = [&](int count) {
        auto t0 = steady_clock::now();
        for (int i = 0; i < count; ++i) db->Put(WriteOptions(), make_key(rng()), val);
        auto dt = duration_cast<microseconds>(steady_clock::now() - t0).count();
        std::cout << "fillrandom: " << count << " writes in " << dt/1000 << " ms = "
                  << std::fixed << std::setprecision(0) << (count*1e6/dt) << " ops/s\n";
    };
    auto run_reads = [&](int count) {
        std::string out;
        int hits = 0;
        auto t0 = steady_clock::now();
        for (int i = 0; i < count; ++i) {
            if (db->Get(ReadOptions(), make_key(rng()), &out).ok()) ++hits;
        }
        auto dt = duration_cast<microseconds>(steady_clock::now() - t0).count();
        std::cout << "readrandom: " << count << " reads (" << hits << " hits) in "
                  << dt/1000 << " ms = "
                  << std::fixed << std::setprecision(0) << (count*1e6/dt) << " ops/s\n";
    };

    if (mode == "fillseq")    run_seq_writes(n);
    else if (mode == "fillrandom") run_rand_writes(n);
    else if (mode == "readrandom") run_reads(n);
    else { run_seq_writes(n); run_rand_writes(n); run_reads(n); }

    // force a flush of any remaining MemTable + wait for compactions to settle
    db->Flush(FlushOptions());
    db->WaitForCompact(WaitForCompactOptions());

    // dump internal statistics
    std::string stats;
    db->GetProperty("rocksdb.stats", &stats);
    std::cout << "\n=== rocksdb.stats ===\n" << stats.substr(0, 3000) << "\n";

    std::cout << "\n=== ticker bytes_written / bytes_read ===\n";
    std::cout << "USER bytes written:     " << opts.statistics->getTickerCount(rocksdb::Tickers::BYTES_WRITTEN) << "\n";
    std::cout << "FLUSH bytes written:    " << opts.statistics->getTickerCount(rocksdb::Tickers::FLUSH_WRITE_BYTES) << "\n";
    std::cout << "COMPACT bytes read:     " << opts.statistics->getTickerCount(rocksdb::Tickers::COMPACT_READ_BYTES) << "\n";
    std::cout << "COMPACT bytes written:  " << opts.statistics->getTickerCount(rocksdb::Tickers::COMPACT_WRITE_BYTES) << "\n";
    std::cout << "BLOOM HITS (useful):    " << opts.statistics->getTickerCount(rocksdb::Tickers::BLOOM_FILTER_USEFUL) << "\n";
    std::cout << "BLOOM FULL POSITIVES:   " << opts.statistics->getTickerCount(rocksdb::Tickers::BLOOM_FILTER_FULL_POSITIVE) << "\n";

    // db destructs
    return 0;
}
