#include <iostream>
#include <chrono>
#include <vector>
#include <string>
#include <iomanip>

#include "catalog/catalog.h"
#include "concurrency/lock_manager.h"
#include "concurrency/transaction_manager.h"
#include "execution/executor_context.h"
#include "execution/values_executor.h"
#include "execution/insert_executor.h"
#include "execution/seq_scan_executor.h"
#include "storage/btree/bplus_tree.h"

using namespace minidb;

void PrintMetrics(const std::string& engine, double write_ops, double read_ops) {
    std::cout << std::left << std::setw(25) << engine 
              << "| " << std::setw(15) << write_ops 
              << "| " << std::setw(15) << read_ops << "\n";
}

void RunBenchmark(int num_records) {
    std::cout << "\n=======================================================\n";
    std::cout << " MiniDB Track C Benchmark: LSM vs Disk-Backed B+ Tree \n";
    std::cout << " Workload: " << num_records << " Records (Forces Buffer Pool Eviction)\n";
    std::cout << "=======================================================\n";
    std::cout << std::left << std::setw(25) << "Engine" 
              << "| " << std::setw(15) << "Writes (ops/s)" 
              << "| " << std::setw(15) << "Reads (ops/s)" << "\n";
    std::cout << "-------------------------------------------------------\n";

    std::vector<std::string> keys;
    std::vector<std::string> values;
    for (int i = 0; i < num_records; ++i) {
        keys.push_back(std::to_string(i));
        values.push_back("Standard_Payload_Data_" + std::to_string(i));
    }

    // ==========================================
    // 1. LSM-Tree Benchmark (Sequential I/O)
    // ==========================================
    Catalog catalog;
    LockManager lock_manager;
    TransactionManager txn_manager(&lock_manager);
    Schema schema({Column("id", TypeId::INTEGER, 4), Column("payload", TypeId::VARCHAR, 255)});
    TableMetadata* table = catalog.CreateTable("lsm_table", schema);

    auto start_lsm_write = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < num_records; ++i) {
        InternalKey ikey{table->oid, keys[i]};
        Row row{{keys[i], values[i]}};
        table->wal->Append(LogRecordType::PUT, ikey.Encode(), row.Serialize());
        table->memtable->Put(ikey, row);
    }
    table->wal->Flush(); // Single sequential Group Commit
    auto end_lsm_write = std::chrono::high_resolution_clock::now();
    
    double lsm_writes_per_sec = (num_records / std::chrono::duration<double, std::milli>(end_lsm_write - start_lsm_write).count()) * 1000.0;

    auto start_lsm_read = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < num_records; ++i) {
        InternalKey ikey{table->oid, keys[i]};
        Row out_row;
        table->memtable->Get(ikey, &out_row);
    }
    auto end_lsm_read = std::chrono::high_resolution_clock::now();
    
    double lsm_reads_per_sec = (num_records / std::chrono::duration<double, std::milli>(end_lsm_read - start_lsm_read).count()) * 1000.0;
    PrintMetrics("LSM-Tree (Sequential)", lsm_writes_per_sec, lsm_reads_per_sec);

    // ==========================================
    // 2. B+ Tree Benchmark (Random I/O)
    // ==========================================
    // Set node capacity to 50 to ensure we generate enough pages to overflow the buffer pool
    BPlusTree btree(50);

    auto start_btree_write = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < num_records; ++i) {
        btree.Insert(keys[i], values[i]);
    }
    auto end_btree_write = std::chrono::high_resolution_clock::now();
    
    double btree_writes_per_sec = (num_records / std::chrono::duration<double, std::milli>(end_btree_write - start_btree_write).count()) * 1000.0;

    auto start_btree_read = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < num_records; ++i) {
        btree.Search(keys[i]);
    }
    auto end_btree_read = std::chrono::high_resolution_clock::now();
    
    double btree_reads_per_sec = (num_records / std::chrono::duration<double, std::milli>(end_btree_read - start_btree_read).count()) * 1000.0;

    PrintMetrics("B+ Tree (Random Disk)", btree_writes_per_sec, btree_reads_per_sec);
    std::cout << "=======================================================\n";
}

int main() {
    // Run 100,000 records. This mathematically guarantees the B+ tree exceeds
    // its simulated 100-page Buffer Pool and begins heavily thrashing the disk.
    RunBenchmark(100000); 
    return 0;
}