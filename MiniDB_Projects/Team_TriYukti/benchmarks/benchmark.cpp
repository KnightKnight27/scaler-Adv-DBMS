#include "sql/executor.h"
#include "transaction/lock_manager.h"
#include "mvcc/mvcc_manager.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <string>
#include <atomic>
#include <fstream>

using namespace minidb;

int main() {
    std::cout << "Running Track B Benchmarks...\n\n";
    
    std::remove("bench.db");
    PageManager pm("bench.db");
    BufferPool bp(1000, &pm);
    LockManager lm;
    MVCCManager mvcc(&bp);
    
    Schema schema;
    schema.columns.push_back({"id", ColumnType::INT, 4});
    schema.columns.push_back({"payload", ColumnType::VARCHAR, 255});
    
    page_id_t first_page;
    bp.UnpinPage(bp.NewPage(&first_page)->GetPageId(), true);
    TableInfo tinfo;
    tinfo.first_page_id = first_page;
    tinfo.last_page_id = first_page;
    tinfo.schema = schema;
    tinfo.index = new BPlusTree(&bp);
    
    std::string payload(100, 'X');
    
    // --- Test 1: Sequential INSERT throughput ---
    std::cout << "--- Test 1: Sequential INSERT ---\n";
    auto t1_start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 10000; ++i) {
        std::vector<std::string> values = {std::to_string(i), payload};
        Tuple tuple = TupleSerializer::Serialize(values, tinfo.schema);
        
        page_id_t pid = tinfo.last_page_id;
        Page* page = bp.FetchPage(pid);
        RecordId rid;
        if (!page->InsertTuple(tuple, &rid)) {
            bp.UnpinPage(pid, false);
            page_id_t new_pid;
            Page* new_page = bp.NewPage(&new_pid, pid);
            new_page->InsertTuple(tuple, &rid);
            bp.UnpinPage(new_pid, true);
            tinfo.last_page_id = new_pid;
            pid = new_pid;
        } else {
            bp.UnpinPage(pid, true);
        }
        tinfo.index->Insert(i, rid);
    }
    auto t1_end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> t1_diff = t1_end - t1_start;
    double t1_ops_sec = 10000 / t1_diff.count();
    std::cout << "10000 Inserts Time: " << t1_diff.count() << " s\n";
    std::cout << "INSERT TPS: " << t1_ops_sec << " ops/sec\n\n";

    // --- Test 2: Index Scan latency ---
    std::cout << "--- Test 2: Index Scan ---\n";
    auto t2_start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 1000; ++i) {
        RecordId res;
        tinfo.index->Search(i * 10, &res);
    }
    auto t2_end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::micro> t2_diff = t2_end - t2_start;
    double avg_latency_us = t2_diff.count() / 1000.0;
    std::cout << "1000 Lookups Time: " << (t2_diff.count() / 1000.0) << " ms\n";
    std::cout << "Avg Latency: " << avg_latency_us << " us/lookup\n\n";

    // --- Test 3: Sequential Scan throughput ---
    std::cout << "--- Test 3: Sequential Scan ---\n";
    auto t3_start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 10; ++i) {
        page_id_t curr = tinfo.first_page_id;
        int count = 0;
        while (curr != INVALID_PAGE_ID) {
            Page* p = bp.FetchPage(curr);
            if (!p) break;
            count += p->GetTupleCount();
            page_id_t next = p->GetNextPageId();
            bp.UnpinPage(curr, false);
            curr = next;
        }
    }
    auto t3_end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> t3_diff = t3_end - t3_start;
    double avg_scan_ms = t3_diff.count() / 10.0;
    std::cout << "10 Scans Time: " << t3_diff.count() << " ms\n";
    std::cout << "Avg Scan Time: " << avg_scan_ms << " ms/scan\n\n";

    // --- Test 4: MVCC vs 2PL read throughput comparison ---
    std::cout << "--- Test 4: MVCC vs 2PL Read Throughput ---\n";
    page_id_t shared_pid;
    bp.UnpinPage(bp.NewPage(&shared_pid)->GetPageId(), true);
    TableInfo shared_tinfo;
    shared_tinfo.first_page_id = shared_pid;
    shared_tinfo.last_page_id = shared_pid;
    shared_tinfo.schema = schema;
    
    Transaction init_txn(1);
    init_txn.SetSnapshotTimestamp(1);
    
    for (int i = 0; i < 1000; ++i) {
        std::vector<std::string> values = {std::to_string(i), payload};
        Tuple tuple = TupleSerializer::Serialize(values, shared_tinfo.schema);
        
        page_id_t pid = shared_tinfo.last_page_id;
        Page* page = bp.FetchPage(pid);
        RecordId rid;
        if (!page->InsertTuple(tuple, &rid)) {
            bp.UnpinPage(pid, false);
            page_id_t new_pid;
            Page* new_page = bp.NewPage(&new_pid, pid);
            new_page->InsertTuple(tuple, &rid);
            bp.UnpinPage(new_pid, true);
            shared_tinfo.last_page_id = new_pid;
            pid = new_pid;
        } else {
            bp.UnpinPage(pid, true);
        }
        mvcc.InsertVersion(rid, tuple, &init_txn);
    }
    mvcc.RecordCommit(init_txn.GetTransactionId(), 1);
    
    // --- Scenario 1: Many concurrent readers (MVCC) ---
    std::cout << "--- Scenario 1: Many concurrent readers (MVCC) ---\n";
    auto s1_start = std::chrono::high_resolution_clock::now();
    std::vector<std::thread> readers_s1;
    for (int i = 0; i < 8; ++i) {
        readers_s1.emplace_back([&]() {
            for (int j = 0; j < 200; ++j) {
                Transaction txn(200 + j);
                txn.SetSnapshotTimestamp(2);
                
                page_id_t curr = shared_tinfo.first_page_id;
                while (curr != INVALID_PAGE_ID) {
                    Page* p = bp.FetchPage(curr);
                    if (!p) break;
                    for (int s = 0; s < p->GetTupleCount(); ++s) {
                        RecordId rid{curr, static_cast<minidb::slot_id_t>(s)};
                        Tuple tmp;
                        mvcc.ReadVisibleVersion(rid, &txn, &tmp);
                    }
                    page_id_t next = p->GetNextPageId();
                    bp.UnpinPage(curr, false);
                    curr = next;
                }
            }
        });
    }
    for (auto& t : readers_s1) t.join();
    auto s1_end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> s1_diff = s1_end - s1_start;
    double s1_ops_sec = 1600.0 / s1_diff.count();
    std::cout << "Scenario 1 Time: " << s1_diff.count() << " s (" << s1_ops_sec << " ops/sec)\n\n";

    // --- Scenario 2: Reader + Writer contention (MVCC) ---
    std::cout << "--- Scenario 2: Reader + Writer contention (MVCC) ---\n";
    auto s2_start = std::chrono::high_resolution_clock::now();
    std::vector<std::thread> threads_s2;
    std::atomic<int> writers_done{0};
    
    // 2 Writers
    for (int i = 0; i < 2; ++i) {
        threads_s2.emplace_back([&]() {
            for (int j = 0; j < 50; ++j) {
                Transaction txn(300 + j);
                std::vector<std::string> values = {std::to_string(j), "WriterUpdate"};
                Tuple tuple = TupleSerializer::Serialize(values, shared_tinfo.schema);
                RecordId rid{shared_tinfo.first_page_id, 0}; // just dummy insert for contention
                mvcc.InsertVersion(rid, tuple, &txn);
                mvcc.RecordCommit(txn.GetTransactionId(), 3); // some ts
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            writers_done++;
        });
    }
    
    // 6 Readers (readers continue without blocking!)
    for (int i = 0; i < 6; ++i) {
        threads_s2.emplace_back([&]() {
            while (writers_done < 2) {
                Transaction txn(400);
                txn.SetSnapshotTimestamp(2);
                page_id_t curr = shared_tinfo.first_page_id;
                while (curr != INVALID_PAGE_ID) {
                    Page* p = bp.FetchPage(curr);
                    if (!p) break;
                    for (int s = 0; s < p->GetTupleCount(); ++s) {
                        RecordId rid{curr, static_cast<minidb::slot_id_t>(s)};
                        Tuple tmp;
                        mvcc.ReadVisibleVersion(rid, &txn, &tmp);
                    }
                    page_id_t next = p->GetNextPageId();
                    bp.UnpinPage(curr, false);
                    curr = next;
                }
            }
        });
    }
    for (auto& t : threads_s2) t.join();
    auto s2_end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> s2_diff = s2_end - s2_start;
    std::cout << "Scenario 2 completed in " << s2_diff.count() << " s (Readers were unblocked by writers)\n\n";

    // --- Scenario 3: Compare against strict 2PL ---
    std::cout << "--- Scenario 3: Compare against strict 2PL ---\n";
    auto t4a_start = std::chrono::high_resolution_clock::now();
    std::vector<std::thread> readers_2pl;
    for (int i = 0; i < 8; ++i) {
        readers_2pl.emplace_back([&]() {
            for (int j = 0; j < 200; ++j) {
                Transaction txn(100 + j);
                lm.LockShared(&txn, "table_read");
                
                page_id_t curr = shared_tinfo.first_page_id;
                while (curr != INVALID_PAGE_ID) {
                    Page* p = bp.FetchPage(curr);
                    if (!p) break;
                    Tuple tmp;
                    for (int s = 0; s < p->GetTupleCount(); ++s) {
                        p->GetTuple(s, &tmp);
                    }
                    page_id_t next = p->GetNextPageId();
                    bp.UnpinPage(curr, false);
                    curr = next;
                }
                
                lm.Unlock(&txn, "table_read");
            }
        });
    }
    for (auto& t : readers_2pl) t.join();
    auto t4a_end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> t4a_diff = t4a_end - t4a_start;
    double t4a_ops_sec = 1600.0 / t4a_diff.count();
    
    std::cout << "2PL Time (Scenario 3): " << t4a_diff.count() << " s (" << t4a_ops_sec << " ops/sec)\n";
    std::cout << "MVCC Time (Scenario 1): " << s1_diff.count() << " s (" << s1_ops_sec << " ops/sec)\n";
    std::cout << "MVCC Speedup ratio (MVCC time / 2PL time): " << s1_diff.count() / t4a_diff.count() << "\n\n";

    std::ofstream out("results.csv");
    out << "Test,Metric,Value\n";
    out << "Sequential INSERT,ops_per_sec," << t1_ops_sec << "\n";
    out << "Index Scan,avg_latency_us," << avg_latency_us << "\n";
    out << "Sequential Scan,avg_scan_ms," << avg_scan_ms << "\n";
    out << "Scenario 1 (MVCC Readers),ops_per_sec," << s1_ops_sec << "\n";
    out << "Scenario 3 (2PL Readers),ops_per_sec," << t4a_ops_sec << "\n";
    out << "MVCC Speedup,ratio," << s1_diff.count() / t4a_diff.count() << "\n";
    out.close();
    
    std::cout << "Results written to results.csv\n";
    return 0;
}
