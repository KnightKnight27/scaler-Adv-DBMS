#include <iostream>
#include <chrono>
#include <cassert>
#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>
#include "common/config.h"
#include "common/rid.h"
#include "storage/disk_manager.h"
#include "storage/slotted_page.h"
#include "storage/buffer_pool_manager.h"
#include "index/b_plus_tree.h"
#include "parser/query_engine.h"
#include "optimizer/cost_based_optimizer.h"
#include "recovery/recovery_manager.h"
#include "replication/replication_manager.h"
#include "replication/replication_receiver.h"

using namespace minidb;

void BenchmarkSlottedPage() {
    std::cout << "=== Running Slotted Page Benchmark ===" << std::endl;
    char page_data[PAGE_SIZE];
    SlottedPage::Init(page_data);

    // 1. Insert Benchmark
    int num_inserts = 200; // Slotted page capacity is small within 4KB
    std::vector<std::string> values;
    for (int i = 0; i < num_inserts; ++i) {
        values.push_back("TupleData_" + std::to_string(i));
    }

    auto start = std::chrono::high_resolution_clock::now();
    int successful_inserts = 0;
    RID rid;
    for (const auto& val : values) {
        if (SlottedPage::InsertTuple(page_data, val, &rid, 0)) {
            successful_inserts++;
        }
    }
    auto end = std::chrono::high_resolution_clock::now();
    double duration_ms = std::chrono::duration<double, std::milli>(end - start).count();

    std::cout << "Inserted " << successful_inserts << " tuples in " << duration_ms << " ms"
              << " (" << (successful_inserts / (duration_ms / 1000.0)) << " ops/sec)" << std::endl;

    // 2. Compaction Benchmark
    // Delete every even slot
    int num_deletes = 0;
    for (int i = 0; i < successful_inserts; i += 2) {
        if (SlottedPage::DeleteTuple(page_data, i)) {
            num_deletes++;
        }
    }

    start = std::chrono::high_resolution_clock::now();
    SlottedPage::CompactPage(page_data);
    end = std::chrono::high_resolution_clock::now();
    duration_ms = std::chrono::duration<double, std::milli>(end - start).count();

    std::cout << "Compacted " << num_deletes << " tombstoned slots in " << duration_ms << " ms" << std::endl;
    std::cout << std::endl;
}

void BenchmarkIndexVsTableScan() {
    std::cout << "=== Running B+ Tree vs Table Scan Benchmark ===" << std::endl;
    std::string db_file = "benchmark_storage.db";
    std::filesystem::remove(db_file);
    std::filesystem::remove(db_file + ".lsns");

    {
        DiskManager disk_manager(db_file);
        BufferPoolManager bpm(50, &disk_manager);
        IntComparator comparator;
        page_id_t root_id = INVALID_PAGE_ID;
        BPlusTree<int, RID, IntComparator> tree(root_id, &bpm, comparator);

        // 1. Build database rows & index
        int num_records = 2000;
        std::vector<page_id_t> pages;
        
        page_id_t pid;
        Page* page = bpm.NewPage(&pid);
        SlottedPage::Init(page->GetData());
        pages.push_back(pid);
        bpm.UnpinPage(pid, true);

        // Populate table pages sequentially
        for (int i = 0; i < num_records; ++i) {
            pid = pages.back();
            page = bpm.FetchPage(pid);
            page->WLock();
            Row r{{{ "id", i }, { "name", "Student_" + std::to_string(i) }}};
            RID rid;
            if (!SlottedPage::InsertTuple(page->GetData(), r.Serialize(), &rid, pid)) {
                page->WUnlock();
                bpm.UnpinPage(pid, false);
                // Create a new page
                page = bpm.NewPage(&pid);
                page->WLock();
                SlottedPage::Init(page->GetData());
                pages.push_back(pid);
                assert(SlottedPage::InsertTuple(page->GetData(), r.Serialize(), &rid, pid));
            }
            page->WUnlock();
            bpm.UnpinPage(pid, true);

            // Insert into B+ tree
            tree.Insert(i, rid);
        }

        // 2. Benchmark Table Scan Lookup
        int num_lookups = 200;
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < num_lookups; ++i) {
            int target_id = (i * 7) % num_records;
            bool found = false;
            // Iterate over all slots in all pages (simulating TableScan)
            for (page_id_t p : pages) {
                Page* pg = bpm.FetchPage(p);
                char* data = pg->GetData();
                uint16_t slots = SlottedPage::GetSlotCount(data);
                for (uint16_t s = 0; s < slots; ++s) {
                    std::string val;
                    if (SlottedPage::GetTuple(data, s, val)) {
                        Row r = Row::Deserialize(val);
                        if (std::get<int>(r.cols["id"]) == target_id) {
                            found = true;
                            break;
                        }
                    }
                }
                bpm.UnpinPage(p, false);
                if (found) break;
            }
        }
        auto end = std::chrono::high_resolution_clock::now();
        double table_scan_ms = std::chrono::duration<double, std::milli>(end - start).count();

        // 3. Benchmark Index Scan Lookup
        start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < num_lookups; ++i) {
            int target_id = (i * 7) % num_records;
            std::vector<RID> res;
            bool found = tree.Find(target_id, &res);
            assert(found);
            Page* pg = bpm.FetchPage(res[0].GetPageId());
            std::string val;
            bool got = SlottedPage::GetTuple(pg->GetData(), res[0].GetSlotNum(), val);
            assert(got);
            Row r = Row::Deserialize(val);
            assert(std::get<int>(r.cols["id"]) == target_id);
            bpm.UnpinPage(res[0].GetPageId(), false);
        }
        end = std::chrono::high_resolution_clock::now();
        double index_scan_ms = std::chrono::duration<double, std::milli>(end - start).count();

        std::cout << "Table Scan lookup time for " << num_lookups << " queries: " << table_scan_ms << " ms"
                  << " (Avg: " << (table_scan_ms / num_lookups) << " ms/query)" << std::endl;
        std::cout << "B+ Tree Index lookup time for " << num_lookups << " queries: " << index_scan_ms << " ms"
                  << " (Avg: " << (index_scan_ms / num_lookups) << " ms/query)" << std::endl;
        std::cout << "Speedup ratio: " << (table_scan_ms / index_scan_ms) << "x" << std::endl;
    }

    std::filesystem::remove(db_file);
    std::filesystem::remove(db_file + ".lsns");
    std::cout << std::endl;
}

void BenchmarkQueryOptimizer() {
    std::cout << "=== Running Query Optimizer DP Scaling Benchmark ===" << std::endl;
    SystemCatalog catalog;
    for (int i = 0; i < 10; ++i) {
        catalog.AddStats(i, {100, 1000, true});
    }

    CostBasedOptimizer optimizer(&catalog);

    // Benchmark DP scaling
    for (int num_tables = 3; num_tables <= 6; ++num_tables) {
        std::vector<table_id_t> tables;
        for (int i = 0; i < num_tables; ++i) {
            tables.push_back(i);
        }
        LogicalQuerySpecification query{tables};

        auto start = std::chrono::high_resolution_clock::now();
        auto plan = optimizer.OptimizeQuery(query);
        auto end = std::chrono::high_resolution_clock::now();
        double duration_us = std::chrono::duration<double, std::micro>(end - start).count();

        std::cout << "Optimized " << num_tables << "-way join query in " << duration_us << " microseconds" << std::endl;
    }
    std::cout << std::endl;
}

void BenchmarkARIESRecovery() {
    std::cout << "=== Running ARIES Recovery Benchmark ===" << std::endl;
    std::string db_file = "recovery_bench.db";
    std::string log_file = "recovery_bench.log";

    std::filesystem::remove(db_file);
    std::filesystem::remove(db_file + ".lsns");
    std::filesystem::remove(log_file);

    int num_ops = 5000;

    {
        DiskManager dm(db_file);
        BufferPoolManager bpm(50, &dm);
        LogManager lm(log_file);

        page_id_t p0;
        Page* pg0 = bpm.NewPage(&p0);
        assert(p0 == 0);
        bpm.UnpinPage(p0, true);
        bpm.FlushAllPages();

        // Log transaction updates
        LogRecord begin_rec(1, 0, LogRecordType::BEGIN);
        lm.AppendRecord(begin_rec);

        lsn_t prev_lsn = begin_rec.lsn;
        for (int i = 0; i < num_ops; ++i) {
            LogRecord upd(1, prev_lsn, LogRecordType::UPDATE, 0, 10, "AAAA", "BBBB");
            lm.AppendRecord(upd);
            prev_lsn = upd.lsn;
        }
    }

    // Now restart and run recovery
    {
        DiskManager dm(db_file);
        BufferPoolManager bpm(50, &dm);
        LogManager lm(log_file);
        RecoveryManager rm(&dm, &bpm, &lm);

        auto start = std::chrono::high_resolution_clock::now();
        rm.RunRecovery();
        auto end = std::chrono::high_resolution_clock::now();
        double duration_ms = std::chrono::duration<double, std::milli>(end - start).count();

        std::cout << "Recovered " << num_ops << " log operations in " << duration_ms << " ms"
                  << " (" << (num_ops / (duration_ms / 1000.0)) << " ops/sec)" << std::endl;
    }

    std::filesystem::remove(db_file);
    std::filesystem::remove(db_file + ".lsns");
    std::filesystem::remove(log_file);
    std::cout << std::endl;
}

void BenchmarkReplication() {
    std::cout << "=== Running Log Replication Benchmark ===" << std::endl;

    std::string primary_db = "repl_bench_p.db";
    std::string replica_db = "repl_bench_r.db";

    std::filesystem::remove(primary_db);
    std::filesystem::remove(primary_db + ".lsns");
    std::filesystem::remove(replica_db);
    std::filesystem::remove(replica_db + ".lsns");

    int listen_port = 23458;
    int num_replications = 500;

    { // Scope for setup
        DiskManager replica_dm(replica_db);
        BufferPoolManager replica_bpm(10, &replica_dm);
        page_id_t p0_repl;
        Page* pg0_repl = replica_bpm.NewPage(&p0_repl);
        SlottedPage::Init(pg0_repl->GetData());
        replica_bpm.UnpinPage(p0_repl, true);
        replica_bpm.FlushAllPages();

        ReplicationReceiver receiver(listen_port, &replica_bpm);
        receiver.StartListening();

        ReplicationManager sync_manager("127.0.0.1", listen_port, ReplicationMode::SYNCHRONOUS);
        sync_manager.StartBroadcasting();
        assert(sync_manager.IsReplicaOnline());

        ReplicationManager async_manager("127.0.0.1", listen_port, ReplicationMode::ASYNCHRONOUS);
        async_manager.StartBroadcasting();
        assert(async_manager.IsReplicaOnline());

        // Benchmark SYNCHRONOUS Log replication
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < num_replications; ++i) {
            LogRecord record(1, 0, LogRecordType::UPDATE, 0, 1, "", "SyncData");
            record.lsn = 100 + i;
            bool ok = sync_manager.ReplicateLog(record);
            assert(ok);
        }
        auto end = std::chrono::high_resolution_clock::now();
        double sync_duration_ms = std::chrono::duration<double, std::milli>(end - start).count();

        // Benchmark ASYNCHRONOUS Log replication
        start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < num_replications; ++i) {
            LogRecord record(1, 0, LogRecordType::UPDATE, 0, 1, "", "AsyncData");
            record.lsn = 200 + i;
            bool ok = async_manager.ReplicateLog(record);
            assert(ok);
        }
        end = std::chrono::high_resolution_clock::now();
        double async_duration_ms = std::chrono::duration<double, std::milli>(end - start).count();

        std::cout << "Synchronous replication of " << num_replications << " logs: " << sync_duration_ms << " ms"
                  << " (" << (num_replications / (sync_duration_ms / 1000.0)) << " ops/sec)" << std::endl;
        std::cout << "Asynchronous replication of " << num_replications << " logs: " << async_duration_ms << " ms"
                  << " (" << (num_replications / (async_duration_ms / 1000.0)) << " ops/sec)" << std::endl;
        std::cout << "Async throughput improvement: " << (sync_duration_ms / async_duration_ms) << "x" << std::endl;
        
        receiver.StopListening();
    }

    std::filesystem::remove(primary_db);
    std::filesystem::remove(primary_db + ".lsns");
    std::filesystem::remove(replica_db);
    std::filesystem::remove(replica_db + ".lsns");
    std::cout << std::endl;
}

int main() {
    std::cout << "============================================" << std::endl;
    std::cout << "      MINIDB ENGINE BENCHMARK RUNNER        " << std::endl;
    std::cout << "============================================" << std::endl;
    std::cout << std::endl;

    BenchmarkSlottedPage();
    BenchmarkIndexVsTableScan();
    BenchmarkQueryOptimizer();
    BenchmarkARIESRecovery();
    BenchmarkReplication();

    std::cout << "BENCHMARK SUITE EXECUTED SUCCESSFULLY!" << std::endl;
    return 0;
}
