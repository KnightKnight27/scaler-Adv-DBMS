/**
 * @file bench_insert.cpp
 * @brief Performance benchmark measuring average insert latency for MiniDB.
 *
 * This benchmark measures the average time required to perform INSERT operations
 * into a HeapFile equipped with a B+ tree index.
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
    constexpr int N_INSERTS = 10000;
    constexpr const char* DB_FILE = "bench_insert.db";
    constexpr const char* WAL_FILE = "bench_insert.wal";

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

    std::cout << "=== Insert Latency Benchmark ===\n";
    std::cout << "Inserting " << N_INSERTS << " records...\n\n";

    auto t0 = std::chrono::high_resolution_clock::now();

    for (int i = 1; i <= N_INSERTS; ++i) {
        TxID txid = txm.begin();
        wal.logBegin(txid);

        Record rec{i, "value_" + std::to_string(i)};
        wal.logInsert(txid, "bench_table", i, rec.value);
        RecordID rid = heap.insertRecord(rec);
        if (rid.isValid()) {
            index.insert(i, rid);
        }

        wal.logCommit(txid);
        txm.commit(txid);
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    double avg_us = (total_ms / N_INSERTS) * 1000.0;

    std::cout << "Total time     : " << total_ms << " ms\n";
    std::cout << "Records written: " << N_INSERTS << "\n";
    std::cout << "Avg per insert : " << avg_us << " μs\n";
    std::cout << "Throughput     : " << (N_INSERTS / (total_ms / 1000.0)) << " inserts/sec\n";
    std::cout << "Pages used     : " << disk.pageCount() << "\n";

    bp.flushAll();
    wal.close();
    disk.close();

    // Cleanup generated database files
    std::remove(DB_FILE);
    std::remove(WAL_FILE);
    return 0;
}
