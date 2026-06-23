#include "../src/page.h"
#include "../src/heap_file.h"
#include "../src/buffer_pool.h"
#include <iostream>
#include <chrono>
#include <vector>
#include <random>
#include <filesystem>

namespace fs = std::filesystem;

int main() {
    std::string filename = "perf_benchmark.dat";
    if (fs::exists(filename)) {
        fs::remove(filename);
    }

    std::cout << "=============================================\n";
    std::cout << "       MiniDB Storage Performance Benchmark  \n";
    std::cout << "=============================================\n\n";

    {
        HeapFile hf(filename);
        BufferPool bp(100, &hf); // Pool capacity 100

        // 1. Benchmark Sequential Page Writes & Allocations
        int num_pages = 1000;
        std::cout << "[PERF] Allocating and writing " << num_pages << " pages sequentially...\n";
        auto start = std::chrono::high_resolution_clock::now();
        
        std::vector<int> pids;
        for (int i = 0; i < num_pages; ++i) {
            int pid = hf.allocatePage();
            pids.push_back(pid);
            Page* pg = bp.getPage(pid);
            if (pg) {
                // Fill data
                std::snprintf(pg->data, PAGE_SIZE, "Bench page %d", pid);
                bp.unpinPage(pid, true);
            }
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> duration = end - start;
        double throughput = num_pages / duration.count();
        std::cout << "       Completed in " << duration.count() << " seconds.\n";
        std::cout << "       Throughput: " << throughput << " page allocations & writes/sec.\n\n";

        // 2. Benchmark Cache Hit Latency (reads from pages currently in buffer pool)
        int cache_hit_ops = 500000;
        std::cout << "[PERF] Performing " << cache_hit_ops << " read ops with 100% cache hits...\n";
        start = std::chrono::high_resolution_clock::now();
        
        // We read page 0 to 99 repeatedly (which are all in pool capacity 100)
        for (int i = 0; i < cache_hit_ops; ++i) {
            int pid = i % 100;
            Page* pg = bp.getPage(pid);
            if (pg) {
                bp.unpinPage(pid, false);
            }
        }
        
        end = std::chrono::high_resolution_clock::now();
        duration = end - start;
        throughput = cache_hit_ops / duration.count();
        std::cout << "       Completed in " << duration.count() << " seconds.\n";
        std::cout << "       Throughput: " << throughput << " cache hits/sec.\n\n";

        // 3. Benchmark Random Read/Write with Cache Evictions (80% read / 20% write)
        int mix_ops = 10000;
        std::cout << "[PERF] Performing " << mix_ops << " random mixed operations over " << num_pages << " pages (triggers evictions)...\n";
        start = std::chrono::high_resolution_clock::now();
        
        std::default_random_engine generator(42);
        std::uniform_int_distribution<int> page_dist(0, num_pages - 1);
        std::uniform_int_distribution<int> op_dist(0, 9); // 0-9 for 80/20 split
        
        for (int i = 0; i < mix_ops; ++i) {
            int pid = page_dist(generator);
            bool is_write = (op_dist(generator) < 2); // 20% write
            
            Page* pg = bp.getPage(pid);
            if (pg) {
                if (is_write) {
                    std::snprintf(pg->data, PAGE_SIZE, "Modified rand bench %d", i);
                }
                bp.unpinPage(pid, is_write);
            }
        }
        
        end = std::chrono::high_resolution_clock::now();
        duration = end - start;
        throughput = mix_ops / duration.count();
        std::cout << "       Completed in " << duration.count() << " seconds.\n";
        std::cout << "       Throughput: " << throughput << " random access page ops/sec.\n\n";
    }

    fs::remove(filename);
    std::cout << "Benchmark complete.\n";
    return 0;
}
