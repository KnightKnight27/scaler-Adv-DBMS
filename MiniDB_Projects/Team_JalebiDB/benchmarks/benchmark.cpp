#include "common/config.h"
#include "common/types.h"
#include "storage/disk_manager.h"
#include "storage/buffer_pool_manager.h"
#include "recovery/log_manager.h"
#include "recovery/recovery_manager.h"
#include "concurrency/lock_manager.h"
#include "concurrency/transaction_manager.h"
#include "execution/catalog.h"
#include "execution/executor.h"
#include "parser/parser.h"
#include "optimizer/optimizer.h"

#include <iostream>
#include <chrono>
#include <vector>
#include <thread>
#include <memory>
#include <fstream>

using namespace minidb;

void CleanDbFiles() {
    std::remove("benchmark_db.db");
    std::remove("benchmark_db.db.log");
}

void RunScanBenchmark() {
    CleanDbFiles();
    std::cout << "[Benchmark] Running B+ Tree vs SeqScan performance benchmark..." << std::endl;
    try {
    
    auto disk_mgr = std::make_unique<DiskManager>("benchmark_db.db");
    auto log_mgr = std::make_unique<LogManager>(disk_mgr.get());
    auto bpm = std::make_unique<BufferPoolManager>(50, disk_mgr.get(), log_mgr.get()); // large buffer pool
    auto lock_mgr = std::make_unique<LockManager>();
    auto txn_mgr = std::make_unique<TransactionManager>(lock_mgr.get(), log_mgr.get(), bpm.get());
    
    Schema schema({
        {"id", TypeId::INT, 4},
        {"val", TypeId::INT, 4}
    });

    page_id_t first_page = bpm->NewPage()->GetPageId();
    page_id_t root_page = bpm->NewPage()->GetPageId();

    BPlusTreeNode root(bpm->FetchPage(root_page));
    root.SetIsLeaf(true);
    root.SetSize(0);
    root.SetParentPageId(INVALID_PAGE_ID);
    root.SetNextPageId(INVALID_PAGE_ID);
    bpm->UnpinPage(root_page, true);

    bpm->UnpinPage(first_page, true);

    Catalog catalog;
    catalog.CreateTable("test", schema, first_page, root_page, "id");
    
    // Populate 1000 tuples
    std::cout << "[Benchmark] Populating 1000 tuples..." << std::endl;
    Transaction *txn = txn_mgr->Begin();
    BPlusTree tree(root_page, bpm.get());

    int count = 1000;
    for (int i = 1; i <= count; ++i) {
        std::vector<Value> vals = {Value(i), Value(i * 10)};
        Tuple t(vals);
        std::string serialized = t.Serialize(schema);

        // Find insertion page
        Page *target_page = nullptr;
        page_id_t pid = first_page;
        RID rid;
        while (true) {
            target_page = bpm->FetchPage(pid);
            SlottedPage slotted(target_page);
            if (slotted.InsertTuple(serialized.data(), serialized.size(), rid)) {
                break;
            }
            
            page_id_t next_page_id = slotted.GetNextPageId();
            if (next_page_id == INVALID_PAGE_ID) {
                Page *new_page = bpm->NewPage();
                next_page_id = new_page->GetPageId();
                slotted.SetNextPageId(next_page_id);
                bpm->UnpinPage(pid, true);
                
                target_page = new_page;
                pid = next_page_id;
                SlottedPage slotted_new(target_page);
                slotted_new.InsertTuple(serialized.data(), serialized.size(), rid);
                break;
            } else {
                bpm->UnpinPage(pid, false);
                pid = next_page_id;
            }
        }

        lock_mgr->AcquireExclusive(txn, rid);
        LogRecord rec(txn->GetTxnId(), txn->GetPrevLSN(), LogRecordType::INSERT, rid, serialized);
        lsn_t lsn = log_mgr->AppendLogRecord(&rec);
        txn->SetPrevLSN(lsn);
        target_page->SetLSN(lsn);

        bpm->UnpinPage(rid.page_id, true);

        tree.Insert(i, rid);
    }
    txn_mgr->Commit(txn);
    bpm->FlushAllPages();
    std::cout << "[Benchmark] Populating finished. Measuring SeqScan..." << std::endl;

    // 1. Measure SeqScan Point Query Time
    TableMetadata *meta = catalog.GetTable("test");
    auto start_seq = std::chrono::high_resolution_clock::now();
    
    int runs = 50;
    for (int r = 0; r < runs; ++r) {
        Transaction *read_txn = txn_mgr->Begin();
        SeqScanExecutor seq_exec(read_txn, meta, WhereOp::EQUALS, "id", Value(500), bpm.get(), lock_mgr.get());
        seq_exec.Init();
        Tuple res;
        RID r_rid;
        while (seq_exec.Next(res, r_rid)) {}
        txn_mgr->Commit(read_txn);
    }
    
    auto end_seq = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> seq_ms = end_seq - start_seq;
    std::cout << "[Benchmark] SeqScan finished. Measuring IndexScan..." << std::endl;

    // 2. Measure IndexScan Point Query Time
    auto start_idx = std::chrono::high_resolution_clock::now();
    for (int r = 0; r < runs; ++r) {
        Transaction *read_txn = txn_mgr->Begin();
        IndexScanExecutor idx_exec(read_txn, meta, "id", Value(500), bpm.get(), lock_mgr.get());
        idx_exec.Init();
        Tuple res;
        RID r_rid;
        while (idx_exec.Next(res, r_rid)) {}
        txn_mgr->Commit(read_txn);
    }
    auto end_idx = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> idx_ms = end_idx - start_idx;

    std::cout << "\n==============================================" << std::endl;
    std::cout << "           SCAN BENCHMARK (1000 Rows)         " << std::endl;
    std::cout << "==============================================" << std::endl;
    std::cout << "  Table Scan (SeqScan) Average: " << (seq_ms.count() / runs) << " ms" << std::endl;
    std::cout << "  Index Scan (IndexScan) Average: " << (idx_ms.count() / runs) << " ms" << std::endl;
    std::cout << "  Index Speedup Factor: " << (seq_ms.count() / idx_ms.count()) << "x" << std::endl;
    std::cout << "==============================================\n" << std::endl;
    } catch (const std::exception &ex) {
        std::cerr << "EXCEPTION IN RUNSCANBENCHMARK: " << ex.what() << std::endl;
    }
}

void RunRecoveryBenchmark() {
    CleanDbFiles();
    std::cout << "[Benchmark] Running Crash Recovery (ARIES) benchmark..." << std::endl;
    
    {
        auto disk_mgr = std::make_unique<DiskManager>("benchmark_db.db");
        auto log_mgr = std::make_unique<LogManager>(disk_mgr.get());
        auto bpm = std::make_unique<BufferPoolManager>(10, disk_mgr.get(), log_mgr.get());
        auto lock_mgr = std::make_unique<LockManager>();
        auto txn_mgr = std::make_unique<TransactionManager>(lock_mgr.get(), log_mgr.get(), bpm.get());
        
        Schema schema({
            {"id", TypeId::INT, 4},
            {"val", TypeId::INT, 4}
        });

        page_id_t first_page = bpm->NewPage()->GetPageId();
        bpm->UnpinPage(first_page, true);

        // Insert 200 items in a transaction but simulate a crash right before committing
        Transaction *txn = txn_mgr->Begin();
        for (int i = 1; i <= 200; ++i) {
            std::vector<Value> vals = {Value(i), Value(i * 100)};
            Tuple t(vals);
            std::string serialized = t.Serialize(schema);

            page_id_t pid = first_page;
            Page *p = nullptr;
            RID rid;
            while (true) {
                p = bpm->FetchPage(pid);
                SlottedPage slotted(p);
                if (slotted.InsertTuple(serialized.data(), serialized.size(), rid)) {
                    break;
                }
                
                page_id_t next_page_id = slotted.GetNextPageId();
                if (next_page_id == INVALID_PAGE_ID) {
                    Page *new_page = bpm->NewPage();
                    next_page_id = new_page->GetPageId();
                    slotted.SetNextPageId(next_page_id);
                    bpm->UnpinPage(pid, true);
                    
                    p = new_page;
                    pid = next_page_id;
                    SlottedPage slotted_new(p);
                    slotted_new.InsertTuple(serialized.data(), serialized.size(), rid);
                    break;
                } else {
                    bpm->UnpinPage(pid, false);
                    pid = next_page_id;
                }
            }

            lock_mgr->AcquireExclusive(txn, rid);
            LogRecord rec(txn->GetTxnId(), txn->GetPrevLSN(), LogRecordType::INSERT, rid, serialized);
            lsn_t lsn = log_mgr->AppendLogRecord(&rec);
            txn->SetPrevLSN(lsn);
            p->SetLSN(lsn);

            bpm->UnpinPage(pid, true);
        }

        // Flush WAL buffer to disk to ensure recovery scans it, then perform simulated crash (close file)
        log_mgr->FlushAll();
    } // destructors called, files closed, threads joined

    // Re-instantiate database and execute recovery
    auto disk_mgr_rec = std::make_unique<DiskManager>("benchmark_db.db");
    auto log_mgr_rec = std::make_unique<LogManager>(disk_mgr_rec.get());
    auto bpm_rec = std::make_unique<BufferPoolManager>(10, disk_mgr_rec.get(), log_mgr_rec.get());

    auto start_rec = std::chrono::high_resolution_clock::now();
    RecoveryManager recovery_mgr(disk_mgr_rec.get(), bpm_rec.get());
    recovery_mgr.RunRecovery();
    auto end_rec = std::chrono::high_resolution_clock::now();
    
    std::chrono::duration<double, std::milli> rec_ms = end_rec - start_rec;

    std::cout << "\n==============================================" << std::endl;
    std::cout << "           RECOVERY BENCHMARK (200 Logs)      " << std::endl;
    std::cout << "==============================================" << std::endl;
    std::cout << "  ARIES Recovery Execution Time: " << rec_ms.count() << " ms" << std::endl;
    std::cout << "==============================================\n" << std::endl;
}

int main() {
    std::cout << "==========================================================" << std::endl;
    std::cout << "             MINIDB PERFORMANCE BENCHMARKS               " << std::endl;
    std::cout << "==========================================================" << std::endl;
    
    RunScanBenchmark();
    RunRecoveryBenchmark();
    
    CleanDbFiles();
    std::cout << "Benchmarks completed successfully." << std::endl;
    return 0;
}
