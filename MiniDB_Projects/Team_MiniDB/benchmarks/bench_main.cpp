// Track C benchmark: row store (heap + B+Tree) vs LSM engine, on the same
// StorageEngine interface. Measures write throughput, point-read latency, and
// space amplification. Results are printed and written to results/bench.csv.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "catalog/catalog.h"
#include "catalog/record.h"
#include "catalog/schema.h"
#include "engine/lsm/lsm_engine.h"
#include "engine/rowstore_engine.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"

using namespace minidb;
using clock_type = std::chrono::steady_clock;
namespace fs = std::filesystem;

struct Result {
    double        write_ops_per_sec = 0;
    double        read_us_per_op    = 0;
    double        read_ops_per_sec  = 0;
    std::uint64_t disk_bytes        = 0;
    std::uint64_t live_rows         = 0;
    double        space_amp         = 0;
};

static double secs(clock_type::time_point a, clock_type::time_point b) {
    return std::chrono::duration<double>(b - a).count();
}

// Drive an engine through the workload. `row_bytes` is the encoded row size used
// for the space-amplification denominator.
static Result run(StorageEngine& eng, const std::string& table, const Schema& schema,
                  const std::vector<std::int64_t>& insert_order,
                  const std::vector<std::int64_t>& read_keys, std::size_t row_bytes) {
    Result r;
    std::string payload(100, 'a');

    // --- write throughput (random-order inserts) ---
    auto t0 = clock_type::now();
    for (std::int64_t k : insert_order)
        eng.put(table, k, Record::serialize(schema, {k, payload}));
    eng.flush();
    auto t1 = clock_type::now();
    r.write_ops_per_sec = insert_order.size() / secs(t0, t1);

    // --- point-read latency (random existing keys) ---
    std::string out;
    auto t2 = clock_type::now();
    for (std::int64_t k : read_keys) eng.get(table, k, out);
    auto t3 = clock_type::now();
    r.read_us_per_op   = secs(t2, t3) * 1e6 / read_keys.size();
    r.read_ops_per_sec = read_keys.size() / secs(t2, t3);

    EngineStats st = eng.stats(table);
    r.disk_bytes = st.bytes_on_disk;
    r.live_rows  = st.live_rows;
    r.space_amp  = static_cast<double>(st.bytes_on_disk) /
                   static_cast<double>(st.live_rows * row_bytes);
    return r;
}

int main(int argc, char** argv) {
    std::size_t N = argc > 1 ? std::stoul(argv[1]) : 200000;  // rows loaded
    std::size_t M = argc > 2 ? std::stoul(argv[2]) : 50000;   // random reads

    Schema schema({{"id", ValueType::INT, 8}, {"data", ValueType::VARCHAR, 120}});
    std::size_t row_bytes = 8 + 2 + 100;  // INT + VARCHAR(len prefix + 100 bytes)

    // Deterministic random insert order and read keys.
    std::vector<std::int64_t> keys(N);
    std::iota(keys.begin(), keys.end(), 1);
    std::mt19937 rng(987654321);
    std::shuffle(keys.begin(), keys.end(), rng);
    std::vector<std::int64_t> reads(M);
    std::uniform_int_distribution<std::int64_t> pick(1, static_cast<std::int64_t>(N));
    for (auto& k : reads) k = pick(rng);

    const std::string outdir = "benchmarks/results";
    fs::create_directories(outdir);

    std::cout << "MiniDB benchmark: N=" << N << " inserts, M=" << M << " random reads\n\n";

    // --- row store ---
    Result row;
    {
        const std::string db = outdir + "/row.db";
        std::remove(db.c_str()); std::remove((db + ".cat").c_str());
        DiskManager disk(db);
        BufferPool  pool(4096, &disk);  // 16 MiB cache
        Catalog     cat(&pool, db + ".cat");
        RowStoreEngine eng(&cat, &pool, &disk);
        eng.create_table("bench", schema, 0);
        row = run(eng, "bench", schema, keys, reads, row_bytes);
    }

    // --- LSM ---
    Result lsm;
    std::uint64_t lsm_disk_after_compaction = 0;
    {
        const std::string dir = outdir + "/lsm";
        fs::remove_all(dir);
        LsmEngine eng(dir, /*memtable=*/4u << 20, /*compaction_trigger=*/4);
        eng.create_table("bench", schema, 0);
        lsm = run(eng, "bench", schema, keys, reads, row_bytes);
        eng.compact("bench");
        lsm_disk_after_compaction = eng.stats("bench").bytes_on_disk;
    }

    auto line = [](const char* metric, double a, double b, const char* unit) {
        std::printf("  %-26s %14.1f %14.1f   %s\n", metric, a, b, unit);
    };
    std::printf("%-28s %14s %14s\n", "metric", "row store", "LSM");
    std::printf("  %s\n", "----------------------------------------------------------");
    line("write throughput", row.write_ops_per_sec, lsm.write_ops_per_sec, "ops/sec");
    line("read latency", row.read_us_per_op, lsm.read_us_per_op, "us/op (lower=better)");
    line("read throughput", row.read_ops_per_sec, lsm.read_ops_per_sec, "ops/sec");
    line("space amplification", row.space_amp, lsm.space_amp, "x (on-disk / live data)");
    std::printf("  LSM space amp after compaction: %.2fx\n",
                static_cast<double>(lsm_disk_after_compaction) /
                static_cast<double>(lsm.live_rows * row_bytes));

    std::ofstream csv(outdir + "/bench.csv", std::ios::trunc);
    csv << "metric,row_store,lsm\n"
        << "write_ops_per_sec," << row.write_ops_per_sec << "," << lsm.write_ops_per_sec << "\n"
        << "read_us_per_op," << row.read_us_per_op << "," << lsm.read_us_per_op << "\n"
        << "read_ops_per_sec," << row.read_ops_per_sec << "," << lsm.read_ops_per_sec << "\n"
        << "space_amp," << row.space_amp << "," << lsm.space_amp << "\n"
        << "lsm_space_amp_after_compaction,,"
        << static_cast<double>(lsm_disk_after_compaction) /
               static_cast<double>(lsm.live_rows * row_bytes) << "\n";
    std::cout << "\nwrote " << outdir << "/bench.csv\n";
    return 0;
}
