/**
 * @file bench_select.cpp
 * @brief Performance benchmark comparing sequential Table Scan vs Index Scan.
 *
 * This benchmark pre-loads N records, then measures and compares point query
 * lookups using a sequential heap file scan vs a B+ tree index search.
 */

#include <chrono>
#include <iostream>
#include <string>

#include "common/config.h"
#include "common/types.h"
#include "index/bplus_tree.h"
#include "recovery/wal.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"
#include "storage/heap_file.h"
#include "transaction/lock_manager.h"
#include "transaction/tx_manager.h"

int main() {
    constexpr int N = 1000;
    constexpr int TARGET = 500;
    constexpr int RUNS = 100;
    constexpr const char* DB_FILE = "bench_select.db";
    constexpr const char* WAL_FILE = "bench_select.wal";

    // Setup DBMS subsystems
    DiskManager disk;
    disk.create(DB_FILE);
    BufferPool bp(disk);
    LockManager lm;
    TxManager txm(lm);
    WAL wal;
    wal.open(WAL_FILE);

    HeapFile heap("bench_table", bp);
    BPlusTree index;
    heap.create();

    // Pre-load N records
    std::cout << "=== Select Benchmark (N=" << N << ", target key=" << TARGET << ") ===\n";
    std::cout << "Loading " << N << " records...\n";

    for (int i = 1; i <= N; ++i) {
        TxID txid = txm.begin();
        wal.logBegin(txid);
        Record rec{i, "val_" + std::to_string(i)};
        wal.logInsert(txid, "bench_table", i, rec.value);
        RecordID rid = heap.insertRecord(rec);
        if (rid.isValid()) {
            index.insert(i, rid);
        }
        wal.logCommit(txid);
        txm.commit(txid);
    }

    std::cout << "Loading done.\n\n";

    // ── Table Scan ───────────────────────────────────────────────────────────
    auto ts_start = std::chrono::high_resolution_clock::now();
    int scan_hits = 0;
    for (int r = 0; r < RUNS; ++r) {
        heap.scanAll([&](const RecordID&, const Record& rec) {
            if (rec.key == TARGET) {
                scan_hits++;
                return false;
            }
            return true;
        });
    }
    auto ts_end = std::chrono::high_resolution_clock::now();
    double scan_ms = std::chrono::duration<double, std::milli>(ts_end - ts_start).count();

    // ── Index Scan ───────────────────────────────────────────────────────────
    auto ti_start = std::chrono::high_resolution_clock::now();
    int idx_hits = 0;
    for (int r = 0; r < RUNS; ++r) {
        auto rid_opt = index.search(TARGET);
        if (rid_opt) {
            idx_hits++;
        }
    }
    auto ti_end = std::chrono::high_resolution_clock::now();
    double idx_ms = std::chrono::duration<double, std::milli>(ti_end - ti_start).count();

    // ── Report ───────────────────────────────────────────────────────────────
    std::cout << "TABLE SCAN  : " << scan_ms / RUNS << " ms/query  (total " << scan_ms << " ms, " << RUNS << " runs)\n";
    std::cout << "INDEX SCAN  : " << idx_ms / RUNS << " ms/query  (total " << idx_ms << " ms, " << RUNS << " runs)\n";
    std::cout << "Speedup     : " << scan_ms / idx_ms << "x (index over scan)\n";
    std::cout << "\nConclusion: For point query key=" << TARGET << " in a table of " << N
              << " records, index scan is " << (scan_ms / idx_ms) << "x faster.\n";

    bp.flushAll();
    wal.close();
    disk.close();

    // Cleanup generated database files
    std::remove(DB_FILE);
    std::remove(WAL_FILE);
    return 0;
}
