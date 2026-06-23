#include "concurrency/transaction_manager.h"
#include "execution/executor.h"
#include "execution/optimizer.h"
#include "index/btree.h"
#include "parser/lexer.h"
#include "parser/parser.h"
#include "storage/buffer_pool_manager.h"
#include "storage/disk_manager.h"
#include "storage/table_heap.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

std::string TempDbPath(const char* suffix) {
    return std::string("/tmp/minidb_bench_") + suffix + "_" +
           std::to_string(static_cast<unsigned long>(std::time(nullptr))) + "_" +
           std::to_string(static_cast<unsigned long>(::getpid())) + ".db";
}

void RemoveFile(const std::string& path) {
    std::remove(path.c_str());
}

double ElapsedMs(const Clock::time_point& start, const Clock::time_point& end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

minidb::RecordId MakeRid(minidb::page_id_t page_id, uint16_t slot_id) {
    minidb::RecordId rid{};
    rid.page_id = page_id;
    rid.slot_id = slot_id;
    return rid;
}

void RunStorageInsertBenchmark() {
    const std::string db_path = TempDbPath("storage");
    RemoveFile(db_path);

    minidb::DiskManager disk(db_path);
    minidb::BufferPoolManager pool(&disk, 64);
    minidb::TableHeap heap(&pool);

    constexpr int kRows = 10000;
    const auto start = Clock::now();
    for (int i = 0; i < kRows; ++i) {
        const std::string key = "row_" + std::to_string(i);
        const std::string value = "payload_" + std::to_string(i);
        heap.InsertVersion(key, value, static_cast<uint64_t>(i + 1));
    }
    const auto end = Clock::now();

    std::cout << "STORAGE_INSERT_ROWS=" << kRows << "\n";
    std::cout << "STORAGE_INSERT_MS=" << ElapsedMs(start, end) << "\n";

    RemoveFile(db_path);
}

void RunBTreeLookupBenchmark() {
    const std::string db_path = TempDbPath("btree");
    RemoveFile(db_path);

    constexpr int kKeys = 50000;
    constexpr int kLookups = 50000;

    {
        minidb::DiskManager disk(db_path);
        minidb::BufferPoolManager pool(&disk, 128);
        minidb::BTree tree(&pool, minidb::BTREE_META_PAGE_ID, 32);

        for (int i = 0; i < kKeys; ++i) {
            tree.Insert(i, MakeRid(static_cast<minidb::page_id_t>(i / 100),
                                   static_cast<uint16_t>(i % 100)));
        }

        std::vector<int> lookup_keys(kLookups);
        for (int i = 0; i < kLookups; ++i) {
            lookup_keys[i] = (i * 7919) % kKeys;
        }

        minidb::RecordId rid{};
        const auto start = Clock::now();
        for (int key : lookup_keys) {
            tree.Search(key, &rid);
        }
        const auto end = Clock::now();

        std::cout << "BTREE_KEYS=" << kKeys << "\n";
        std::cout << "BTREE_LOOKUPS=" << kLookups << "\n";
        std::cout << "BTREE_LOOKUP_MS=" << ElapsedMs(start, end) << "\n";
    }

    RemoveFile(db_path);
}

void RunMvccConcurrentReadBenchmark() {
    minidb::TransactionManager tm;

    const minidb::TxID setup = tm.Begin();
    for (int i = 0; i < 1000; ++i) {
        tm.Insert(setup, "key_" + std::to_string(i), std::to_string(i));
    }
    tm.Commit(setup);

    constexpr int kReaders = 8;
    constexpr int kReadsPerThread = 5000;

    const auto start = Clock::now();
    std::vector<std::thread> readers;
    readers.reserve(kReaders);
    for (int t = 0; t < kReaders; ++t) {
        readers.emplace_back([&tm, t]() {
            for (int i = 0; i < kReadsPerThread; ++i) {
                const minidb::TxID reader = tm.Begin();
                const std::string key = "key_" + std::to_string((i + t) % 1000);
                (void)tm.Read(reader, key);
                tm.Commit(reader);
            }
        });
    }
    for (std::thread& thread : readers) {
        thread.join();
    }
    const auto end = Clock::now();

    const int total_reads = kReaders * kReadsPerThread;
    const double elapsed = ElapsedMs(start, end);

    std::cout << "MVCC_READERS=" << kReaders << "\n";
    std::cout << "MVCC_READS_PER_THREAD=" << kReadsPerThread << "\n";
    std::cout << "MVCC_TOTAL_READS=" << total_reads << "\n";
    std::cout << "MVCC_CONCURRENT_READ_MS=" << elapsed << "\n";
    std::cout << "MVCC_READS_PER_SEC=" << (total_reads / (elapsed / 1000.0)) << "\n";
}

void SeedLargeUsersTable(minidb::QueryEngine& engine, int row_count) {
    for (int i = 1; i <= row_count; ++i) {
        const std::string sql = "INSERT INTO users (id, name, age) VALUES (" +
                                std::to_string(i) + ", 'user" + std::to_string(i) + "', " +
                                std::to_string(20 + (i % 50)) + ")";
        (void)engine.ExecuteSql(sql);
    }
}

double RunSelectBenchmark(minidb::QueryEngine& engine, const std::string& sql, int repeats) {
    double total_ms = 0.0;
    for (int i = 0; i < repeats; ++i) {
        const auto start = Clock::now();
        (void)engine.ExecuteSql(sql);
        total_ms += ElapsedMs(start, Clock::now());
    }
    return total_ms / static_cast<double>(repeats);
}

void RunScanBenchmark() {
    minidb::TransactionManager tm;
    minidb::QueryEngine engine(&tm);

    constexpr int kRows = 5000;
    SeedLargeUsersTable(engine, kRows);

    constexpr int kRepeats = 20;
    const double index_ms =
        RunSelectBenchmark(engine, "SELECT name FROM users WHERE id = 2500", kRepeats);
    const double seq_ms =
        RunSelectBenchmark(engine, "SELECT name FROM users WHERE age > 40", kRepeats);

    std::cout << "SCAN_TABLE_ROWS=" << kRows << "\n";
    std::cout << "SCAN_INDEX_AVG_MS=" << index_ms << "\n";
    std::cout << "SCAN_SEQ_AVG_MS=" << seq_ms << "\n";
    std::cout << "SCAN_SPEEDUP=" << (seq_ms / index_ms) << "\n";
}

}  // namespace

int main() {
    std::cout << "BENCHMARK_BUILD=Release\n";
    std::cout << "BENCHMARK_TIMESTAMP=" << std::time(nullptr) << "\n";

    RunStorageInsertBenchmark();
    RunBTreeLookupBenchmark();
    RunMvccConcurrentReadBenchmark();
    RunScanBenchmark();

    return 0;
}
