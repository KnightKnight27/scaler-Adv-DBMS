// bench_insert.cpp – measures insert throughput
#include <iostream>
#include <chrono>
#include <string>
#include <filesystem>
#include "common/config.h"
#include "storage/disk_manager.h"
#include "storage/buffer_pool.h"
#include "storage/heap_file.h"
#include "index/bplus_tree.h"
#include "recovery/wal.h"
#include "transaction/lock_manager.h"
#include "transaction/tx_manager.h"
#include "replication/primary.h"

using namespace minidb;
using namespace std::chrono;

int main() {
    // Use temporary files so benchmarks don't corrupt normal data.
    const std::string DB  = "bench_primary.db";
    const std::string wal_path = "bench.wal";

    DiskManager  dm(DB);
    BufferPool   bp(DEFAULT_POOL_SIZE, &dm);
    HeapFile     heap(&bp, INVALID_PAGE_ID);
    BPlusTree    tree;
    WAL          wal(wal_path);
    LockManager  lm;
    TxManager    txm(&lm, &wal);
    Primary      primary(&heap, &tree, &wal, &lm, &txm);

    constexpr int N = 10000;
    auto t0 = high_resolution_clock::now();

    for (int i = 1; i <= N; ++i) {
        primary.Execute("INSERT " + std::to_string(i) + " " + std::to_string(i * 10));
    }

    auto t1 = high_resolution_clock::now();
    double ms = duration_cast<microseconds>(t1 - t0).count() / 1000.0;

    std::cout << "=== INSERT Benchmark ===\n";
    std::cout << "Rows inserted : " << N << "\n";
    std::cout << "Total time    : " << ms << " ms\n";
    std::cout << "Throughput    : " << (N / (ms / 1000.0)) << " inserts/sec\n";
    std::cout << "Avg latency   : " << (ms / N) << " ms/op\n";

    // Cleanup temp files
    std::filesystem::remove(DB);
    std::filesystem::remove(wal_path);
    return 0;
}
