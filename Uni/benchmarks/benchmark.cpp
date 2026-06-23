#include "db.h"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <random>
#include <mutex>
#include <iomanip>

struct ThreadMetrics {
    int commits = 0;
    int aborts = 0;
    long long total_duration_us = 0; // micro-seconds
};

void RunWorkload(Database* db, int duration_sec, int write_pct, int thread_id, ThreadMetrics& metrics) {
    std::random_device rd;
    std::mt19937 gen(rd() ^ (thread_id * 1000));
    std::uniform_int_distribution<> pct_dist(0, 99);
    std::uniform_int_distribution<> key_dist(1, 1000);

    auto start_time = std::chrono::steady_clock::now();
    auto end_time = start_time + std::chrono::seconds(duration_sec);

    int query_id = thread_id * 10000;

    while (std::chrono::steady_clock::now() < end_time) {
        auto tx_start = std::chrono::steady_clock::now();
        Transaction* txn = db->BeginTransaction();
        
        bool success = true;
        int is_write = pct_dist(gen) < write_pct;

        if (is_write) {
            // Write: INSERT INTO users or orders
            int id = key_dist(gen) + query_id++;
            std::string name = "User_" + std::to_string(id);
            int age = 20 + (id % 50);
            
            std::string sql = "INSERT INTO users VALUES (" + std::to_string(id) + ", '" + name + "', " + std::to_string(age) + ")";
            std::vector<Tuple> rows;
            std::vector<std::string> schema;
            success = db->ExecuteSQL(txn, sql, rows, schema);
        } else {
            // Read: SELECT users JOIN orders
            int id = key_dist(gen);
            // Query with JOIN and WHERE
            std::string sql = "SELECT users.name, orders.product FROM users JOIN orders ON users.id = orders.user_id WHERE users.id = " + std::to_string(id);
            std::vector<Tuple> rows;
            std::vector<std::string> schema;
            success = db->ExecuteSQL(txn, sql, rows, schema);
        }

        if (success) {
            bool ok = db->CommitTransaction(txn);
            auto tx_end = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(tx_end - tx_start).count();
            if (ok) {
                metrics.commits++;
                metrics.total_duration_us += duration;
            } else {
                metrics.aborts++;
            }
        } else {
            db->AbortTransaction(txn);
            metrics.aborts++;
        }

        // Small sleep to control extreme CPU thrashing
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void BenchmarkMode(ConcurrencyMode mode, const std::string& mode_name, int num_threads, int duration_sec, int write_pct) {
    std::string db_file = "bench_" + mode_name + ".db";
    std::string log_file = "bench_" + mode_name + ".log";
    
    // Clear files
    {
        std::ofstream d(db_file, std::ios::trunc);
        std::ofstream l(log_file, std::ios::trunc);
    }

    std::cout << "[Benchmark] Booting MiniDB in " << mode_name << " mode with " << num_threads << " threads..." << std::endl;
    
    std::unique_ptr<Database> db = std::make_unique<Database>(db_file, log_file, mode);
    db->CreateTable("users", {"users.id", "users.name", "users.age"});
    db->CreateTable("orders", {"orders.id", "orders.user_id", "orders.product"});

    // Pre-populate some records
    Transaction* init_txn = db->BeginTransaction();
    std::vector<Tuple> temp_rows;
    std::vector<std::string> temp_schema;
    for (int i = 1; i <= 200; ++i) {
        db->ExecuteSQL(init_txn, "INSERT INTO users VALUES (" + std::to_string(i) + ", 'PrePop_" + std::to_string(i) + "', " + std::to_string(20 + i % 40) + ")", temp_rows, temp_schema);
        db->ExecuteSQL(init_txn, "INSERT INTO orders VALUES (" + std::to_string(i) + ", " + std::to_string(i) + ", 'Item_" + std::to_string(i) + "')", temp_rows, temp_schema);
    }
    db->CommitTransaction(init_txn);
    db->GetBufferPoolManager()->FlushAllPages();

    std::vector<std::thread> threads;
    std::vector<ThreadMetrics> metrics(num_threads);

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < num_threads; ++i) {
        threads.push_back(std::thread(RunWorkload, db.get(), duration_sec, write_pct, i, std::ref(metrics[i])));
    }

    for (auto& t : threads) {
        t.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    double total_time_sec = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() / 1000.0;

    // Aggregate statistics
    int total_commits = 0;
    int total_aborts = 0;
    long long sum_durations = 0;

    for (int i = 0; i < num_threads; ++i) {
        total_commits += metrics[i].commits;
        total_aborts += metrics[i].aborts;
        sum_durations += metrics[i].total_duration_us;
    }

    double tps = total_commits / total_time_sec;
    double avg_lat_ms = (total_commits > 0) ? (double)sum_durations / total_commits / 1000.0 : 0.0;
    double abort_rate = (total_commits + total_aborts > 0) ? (double)total_aborts / (total_commits + total_aborts) * 100.0 : 0.0;

    std::cout << "\n=============================================" << std::endl;
    std::cout << " RESULTS: " << mode_name << " Benchmark" << std::endl;
    std::cout << "=============================================" << std::endl;
    std::cout << "  Duration         : " << total_time_sec << " seconds" << std::endl;
    std::cout << "  Client Threads   : " << num_threads << std::endl;
    std::cout << "  Write Ratio      : " << write_pct << "%" << std::endl;
    std::cout << "  Commits          : " << total_commits << std::endl;
    std::cout << "  Aborts           : " << total_aborts << " (Deadlocks / Conflicts)" << std::endl;
    std::cout << "  Abort Rate       : " << std::fixed << std::setprecision(2) << abort_rate << "%" << std::endl;
    std::cout << "  Throughput (TPS) : " << std::fixed << std::setprecision(2) << tps << " txn/sec" << std::endl;
    std::cout << "  Avg Latency      : " << std::fixed << std::setprecision(2) << avg_lat_ms << " ms" << std::endl;
    std::cout << "=============================================\n" << std::endl;
}

int main() {
    std::cout << "=========================================================" << std::endl;
    std::cout << "   MiniDB Concurrency Benchmark Suite (2PL vs MVCC)     " << std::endl;
    std::cout << "=========================================================" << std::endl;

    int duration = 2; // Duration in seconds per run
    int threads = 4;   // Concurrent threads
    int write_ratio = 30; // 30% inserts, 70% joins/selects (medium-high contention)

    // Run Strict 2PL Benchmark
    BenchmarkMode(ConcurrencyMode::TWO_PHASE_LOCKING, "2PL", threads, duration, write_ratio);

    // Run MVCC Benchmark
    BenchmarkMode(ConcurrencyMode::MULTI_VERSION_CONCURRENCY_CONTROL, "MVCC", threads, duration, write_ratio);

    std::cout << "[Benchmark Suite] Completed successfully." << std::endl;
    return 0;
}
