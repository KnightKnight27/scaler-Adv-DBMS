#include <iostream>
#include <chrono>
#include <filesystem>
#include "storage/heap_file.h"
#include "storage_lsm/memtable.h"
#include "storage_lsm/sstable.h"
#include "common/types.h"

using namespace minidb;

int main() {
    std::cout << "========================================\n";
    std::cout << " MiniDB Benchmark: HeapFile vs LSM \n";
    std::cout << "========================================\n";
    
    int num_rows = 100000; // 100k rows
    std::cout << "Workload: " << num_rows << " sequential inserts.\n\n";
    
    Schema schema({Column("id", ColumnType::INT), Column("val", ColumnType::VARCHAR)});
    
    // --- HeapFile Benchmark ---
    std::filesystem::remove("bench_heap.db");
    HeapFile hf("bench_heap.db");
    
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < num_rows; i++) {
        auto t = serialize_tuple(Tuple({i, std::string("bench_val")}), schema);
        hf.insert_tuple(t.data(), t.size());
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto heap_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::cout << "HeapFile Insert Time: " << heap_time << " ms\n";

    // --- LSM Benchmark ---
    MemTable mem;
    start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < num_rows; i++) {
        mem.put(i, "bench_val");
        if (mem.size() >= 10000) {
            SSTable::write_from_memtable("bench_sst_" + std::to_string(i) + ".db", mem);
            mem.clear();
        }
    }
    if (mem.size() > 0) {
        SSTable::write_from_memtable("bench_sst_final.db", mem);
    }
    end = std::chrono::high_resolution_clock::now();
    auto lsm_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::cout << "LSM Insert Time:      " << lsm_time << " ms\n\n";
    
    std::cout << "Conclusion:\n";
    std::cout << "LSM typically handles sequential batch writes in-memory much faster,\n";
    std::cout << "whereas HeapFile requires continuous disk I/O and slot management.\n";

    // Cleanup
    std::filesystem::remove("bench_heap.db");
    for (int i = 0; i < num_rows; i++) {
        std::filesystem::remove("bench_sst_" + std::to_string(i) + ".db");
    }
    std::filesystem::remove("bench_sst_final.db");

    return 0;
}
