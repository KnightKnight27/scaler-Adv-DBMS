#include <iostream>
#include <chrono>
#include <vector>
#include <string>
#include <filesystem>

#include "catalog/catalog.h"
#include "concurrency/lock_manager.h"
#include "concurrency/transaction_manager.h"
#include "execution/executor_context.h"
#include "execution/values_executor.h"
#include "execution/insert_executor.h"
#include "execution/seq_scan_executor.h"

using namespace minidb;

void RunBenchmark(int num_records) {
    std::cout << "==========================================\n";
    std::cout << " MiniDB LSM-Tree Benchmark \n";
    std::cout << " Records: " << num_records << "\n";
    std::cout << "==========================================\n";

    Catalog catalog;
    LockManager lock_manager;
    TransactionManager txn_manager(&lock_manager);

    Schema schema({
        Column("id", TypeId::INTEGER, 4),
        Column("payload", TypeId::VARCHAR, 255)
    });
    TableMetadata* table = catalog.CreateTable("bench_table", schema);

    // --- 1. WRITE THROUGHPUT BENCHMARK ---
    std::vector<Row> insert_data;
    for (int i = 0; i < num_records; ++i) {
        insert_data.push_back(Row{{std::to_string(i), "TestPayloadData_" + std::to_string(i)}});
    }

    Transaction* write_txn = txn_manager.Begin();
    ExecutorContext write_ctx(&catalog, write_txn->GetTransactionId());
    
    auto values_node = std::make_unique<ValuesExecutor>(&write_ctx, insert_data);
    InsertExecutor insert_node(&write_ctx, std::move(values_node), table->oid);

    auto start_write = std::chrono::high_resolution_clock::now();
    
    insert_node.Init();
    Row dummy_row;
    insert_node.Next(&dummy_row);
    
    txn_manager.Commit(write_txn);
    auto end_write = std::chrono::high_resolution_clock::now();
    
    std::chrono::duration<double, std::milli> write_time = end_write - start_write;
    double writes_per_sec = (num_records / write_time.count()) * 1000.0;

    std::cout << "[WRITE] Total Time: " << write_time.count() << " ms\n";
    std::cout << "[WRITE] Throughput: " << writes_per_sec << " ops/sec\n";

    // --- 2. READ LATENCY (SCAN) BENCHMARK ---
    Transaction* read_txn = txn_manager.Begin();
    ExecutorContext read_ctx(&catalog, read_txn->GetTransactionId());
    SeqScanExecutor scan_node(&read_ctx, table->oid);

    auto start_read = std::chrono::high_resolution_clock::now();
    
    scan_node.Init();
    int read_count = 0;
    while (scan_node.Next(&dummy_row)) {
        read_count++;
    }
    
    txn_manager.Commit(read_txn);
    auto end_read = std::chrono::high_resolution_clock::now();
    
    std::chrono::duration<double, std::milli> read_time = end_read - start_read;
    double reads_per_sec = (num_records / read_time.count()) * 1000.0;

    std::cout << "[READ]  Total Time: " << read_time.count() << " ms\n";
    std::cout << "[READ]  Throughput: " << reads_per_sec << " ops/sec\n";
    std::cout << "==========================================\n";
}

int main() {
    RunBenchmark(10000); // Test with 10,000 records
    return 0;
}