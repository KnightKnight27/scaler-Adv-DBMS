#include "../src/database.h"
#include "../src/distributed/node.h"
#include "../src/distributed/replication.h"
#include <iostream>
#include <chrono>
#include <vector>
#include <atomic>

void run_benchmark() {
    std::cout << "========================================" << std::endl;
    std::cout << "        MiniDB Performance Benchmark    " << std::endl;
    std::cout << "========================================" << std::endl;
    
    std::string test_dir = "./benchmark_db";
    fs_compat::remove_all(test_dir);
    
    Database db(test_dir, true); // use WAL
    
    // Warmup & stats init
    db.execute_update("INSERT INTO students VALUES (1, 'Alice', 20)");
    
    // 1. Measure INSERT latency
    const int num_inserts = 200;
    std::cout << "Running " << num_inserts << " serial INSERT operations..." << std::endl;
    auto start_insert = std::chrono::high_resolution_clock::now();
    for (int i = 2; i <= num_inserts + 1; ++i) {
        std::string sql = "INSERT INTO students VALUES (" + std::to_string(i) + ", 'Student" + std::to_string(i) + "', " + std::to_string(20 + (i % 10)) + ")";
        db.execute_update(sql);
    }
    auto end_insert = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> insert_dur = end_insert - start_insert;
    double avg_insert_ms = insert_dur.count() / num_inserts;
    double insert_tps = num_inserts / (insert_dur.count() / 1000.0);
    std::cout << "INSERT Avg Latency: " << avg_insert_ms << " ms" << std::endl;
    std::cout << "INSERT Throughput:  " << insert_tps << " TPS" << std::endl;
    
    // 2. Measure SELECT latency
    const int num_selects = 500;
    std::cout << "Running " << num_selects << " serial SELECT operations..." << std::endl;
    auto start_select = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < num_selects; ++i) {
        int target_id = 1 + (i % num_inserts);
        std::string sql = "SELECT id, name FROM students WHERE id = " + std::to_string(target_id);
        db.execute_query(sql);
    }
    auto end_select = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> select_dur = end_select - start_select;
    double avg_select_ms = select_dur.count() / num_selects;
    double select_tps = num_selects / (select_dur.count() / 1000.0);
    std::cout << "SELECT Avg Latency: " << avg_select_ms << " ms" << std::endl;
    std::cout << "SELECT Throughput:  " << select_tps << " QPS" << std::endl;
    
    // 3. Concurrent Read Scale-Out Throughput
    std::cout << "Running concurrent SELECT operations (4 threads)..." << std::endl;
    std::atomic<int> query_count{0};
    std::atomic<bool> keep_running{true};
    std::vector<Thread*> threads;
    
    auto thread_func = [&db, &query_count, &keep_running, num_inserts]() {
        int i = 0;
        while (keep_running) {
            int target_id = 1 + ((i++) % num_inserts);
            std::string sql = "SELECT id, name FROM students WHERE id = " + std::to_string(target_id);
            try {
                db.execute_query(sql);
                query_count++;
            } catch (...) {}
        }
    };
    
    auto start_concurrent = std::chrono::high_resolution_clock::now();
    for (int t = 0; t < 4; ++t) {
        threads.push_back(new Thread(thread_func));
    }
    
    Sleep(2000);
    keep_running = false;
    for (auto* thread : threads) {
        thread->join();
        delete thread;
    }
    auto end_concurrent = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> concurrent_dur = end_concurrent - start_concurrent;
    double concurrent_qps = query_count / concurrent_dur.count();
    std::cout << "Concurrent SELECT QPS: " << concurrent_qps << " QPS" << std::endl;
    
    fs_compat::remove_all(test_dir);
    std::cout << "========================================" << std::endl;
}

int main() {
    run_benchmark();
    return 0;
}
