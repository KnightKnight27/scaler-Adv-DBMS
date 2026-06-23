// bench_select.cpp – compares INDEX_SCAN vs TABLE_SCAN latency
#include <iostream>
#include <chrono>
#include <string>
#include <filesystem>
#include "common/config.h"
#include "storage/disk_manager.h"
#include "storage/buffer_pool.h"
#include "storage/heap_file.h"
#include "index/bplus_tree.h"
#include "query/executor.h"
#include "query/optimizer.h"
#include "query/parser.h"
#include "recovery/wal.h"
#include "transaction/lock_manager.h"
#include "transaction/tx_manager.h"
#include "replication/primary.h"

using namespace minidb;
using namespace std::chrono;

int main() {
    const std::string DB  = "bench_sel.db";
    const std::string wal_path = "bench_sel.wal";

    DiskManager  dm(DB);
    BufferPool   bp(DEFAULT_POOL_SIZE, &dm);
    HeapFile     heap(&bp, INVALID_PAGE_ID);
    BPlusTree    tree;
    WAL          wal(wal_path);
    LockManager  lm;
    TxManager    txm(&lm, &wal);
    Primary      primary(&heap, &tree, &wal, &lm, &txm);
    Optimizer    opt;
    Executor     exec(&heap, &tree);

    constexpr int N = 5000;

    // Populate
    for (int i = 1; i <= N; ++i)
        primary.Execute("INSERT " + std::to_string(i) + " " + std::to_string(i));

    // ── INDEX_SCAN benchmark ─────────────────────────────────────────────────
    auto t0 = high_resolution_clock::now();
    for (int i = 1; i <= N; ++i) {
        Statement s = Parser::Parse("SELECT " + std::to_string(i));
        exec.Execute(s, "INDEX_SCAN");
    }
    auto t1 = high_resolution_clock::now();
    double idx_ms = duration_cast<microseconds>(t1 - t0).count() / 1000.0;

    // ── TABLE_SCAN benchmark ─────────────────────────────────────────────────
    t0 = high_resolution_clock::now();
    for (int i = 1; i <= N; ++i) {
        Statement s = Parser::Parse("SELECT " + std::to_string(i));
        exec.Execute(s, "TABLE_SCAN");
    }
    t1 = high_resolution_clock::now();
    double tbl_ms = duration_cast<microseconds>(t1 - t0).count() / 1000.0;

    std::cout << "=== SELECT Benchmark (N=" << N << " lookups) ===\n";
    std::cout << "INDEX_SCAN total : " << idx_ms << " ms  avg=" << (idx_ms/N) << " ms\n";
    std::cout << "TABLE_SCAN total : " << tbl_ms << " ms  avg=" << (tbl_ms/N) << " ms\n";
    std::cout << "Speedup          : " << (tbl_ms / idx_ms) << "x\n";

    std::filesystem::remove(DB);
    std::filesystem::remove(wal_path);
    return 0;
}
