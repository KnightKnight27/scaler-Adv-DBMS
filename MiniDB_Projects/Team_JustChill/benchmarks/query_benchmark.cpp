#include <chrono>
#include <iostream>
#include <string>
#include <vector>
#include <numeric>
#include <algorithm>
#include <thread>
#include <fstream>
#include <filesystem>
#include "execution.h"
#include "replication.h"
#include "wal.h"

using namespace minidb;
using Clock = std::chrono::steady_clock;

// Formatted benchmark result output helper
void printReport(const std::string& name, const std::vector<double>& latencies) {
    if (latencies.empty()) return;
    double total = std::accumulate(latencies.begin(), latencies.end(), 0.0);
    double avg = total / latencies.size();
    
    std::vector<double> sorted = latencies;
    std::sort(sorted.begin(), sorted.end());
    double p50 = sorted[sorted.size() * 0.5];
    double p90 = sorted[sorted.size() * 0.9];
    double p99 = sorted[sorted.size() * 0.99];
    double qps = latencies.size() / (total / 1000000.0); // total is in microseconds

    std::cout << "  * " << name << ":\n";
    std::cout << "    - Throughput: " << static_cast<long>(qps) << " QPS\n";
    std::cout << "    - Latency (Avg): " << avg << " us\n";
    std::cout << "    - Latency (p50): " << p50 << " us\n";
    std::cout << "    - Latency (p90): " << p90 << " us\n";
    std::cout << "    - Latency (p99): " << p99 << " us\n\n";
}

int main() {
    std::cout << "=============================================\n";
    std::cout << "       MiniDB Systems-Grade Query Benchmark \n";
    std::cout << "=============================================\n\n";

    Catalog catalog;
    LockManager lock_mgr;
    ExecContext ctx{&lock_mgr, /*txn=*/1};

    Table* users = catalog.createTable(
        "users", {{"id", ValueType::Int}, {"name", ValueType::Text}}, 0);

    const int N = 10000;
    std::vector<double> latencies;
    latencies.reserve(N);

    // 1. Write Storm: 10,000 sequential inserts
    std::cout << "[WORKLOAD] Executing Write Storm (10,000 INSERT queries)...\n";
    for (int k = 0; k < N; ++k) {
        auto t0 = Clock::now();
        Insert ins(users, {Value::Int(k), Value::Text("user_" + std::to_string(k))}, ctx);
        ins.open();
        Tuple sink;
        while (ins.next(sink)) {}
        ins.close();
        auto t1 = Clock::now();
        latencies.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
    }
    printReport("Write Storm (INSERT)", latencies);
    latencies.clear();

    // 2. Point Lookups: 10,000 select queries hitting the primary key index
    std::cout << "[WORKLOAD] Executing Point Lookups (10,000 SELECT by PK)...\n";
    for (int k = 0; k < N; ++k) {
        auto t0 = Clock::now();
        IndexScan scan(users, /*low=*/k, /*high=*/k, ctx);
        scan.open();
        Tuple row;
        while (scan.next(row)) {}
        scan.close();
        auto t1 = Clock::now();
        latencies.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
    }
    printReport("Point Lookups (SELECT PK)", latencies);
    latencies.clear();

    // 3. Full Table Scans: 100 scans traversing the entire table
    std::cout << "[WORKLOAD] Executing Full Table Scans (100 sequential table scans)...\n";
    for (int k = 0; k < 100; ++k) {
        auto t0 = Clock::now();
        TableScan scan(users, ctx);
        scan.open();
        Tuple row;
        while (scan.next(row)) {}
        scan.close();
        auto t1 = Clock::now();
        latencies.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
    }
    printReport("Full Table Scans (SELECT *)", latencies);
    latencies.clear();

    // 4. Mixed CRUD: 70% Point Lookups / 30% Inserts (10,000 ops sequentially)
    std::cout << "[WORKLOAD] Executing Mixed CRUD (7,000 SELECT / 3,000 INSERT)...\n";
    for (int k = 0; k < 10000; ++k) {
        bool is_insert = (k % 10 < 3); // 30% inserts
        auto t0 = Clock::now();
        if (is_insert) {
            Insert ins(users, {Value::Int(N + k), Value::Text("user_" + std::to_string(N + k))}, ctx);
            ins.open();
            Tuple sink;
            while (ins.next(sink)) {}
            ins.close();
        } else {
            int key = k % N;
            IndexScan scan(users, key, key, ctx);
            scan.open();
            Tuple row;
            while (scan.next(row)) {}
            scan.close();
        }
        auto t1 = Clock::now();
        latencies.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
    }
    printReport("Mixed CRUD (70/30)", latencies);
    latencies.clear();

    lock_mgr.release_all(ctx.txn);

    // 5. Synchronous Replication Network Overhead Benchmark
    std::cout << "[REPLICATION] Starting loopback replica node on port 9995...\n";
    ReplicationNode mock_replica("127.0.0.1", 9995);
    std::thread replica_thread([&]() {
        try {
            mock_replica.startReplicaServer([](const std::string&) {
                // Mock replication write callback
            });
        } catch (...) {}
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    ReplicationNode primary_node("127.0.0.1", 9995);
    std::cout << "[REPLICATION] Measuring network overhead of sendLogToReplica (10,000 statements)...\n";
    
    std::vector<double> repl_latencies;
    repl_latencies.reserve(N);
    for (int k = 0; k < N; ++k) {
        std::string sql = "INSERT INTO students VALUES (" + std::to_string(k) + ", 'user_" + std::to_string(k) + "');";
        auto t0 = Clock::now();
        bool success = primary_node.sendLogToReplica(sql);
        auto t1 = Clock::now();
        if (success) {
            repl_latencies.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        }
    }

    mock_replica.stopReplicaServer();
    if (replica_thread.joinable()) {
        replica_thread.join();
    }

    printReport("Replication Overhead (TCP loopback sendLog)", repl_latencies);

    // 6. Crash Recovery timing benchmark
    std::cout << "[RECOVERY] Preparing WAL log with 10,000 INSERT entries...\n";
    std::string wal_file = "benchmark_wal.log";
    std::ofstream out(wal_file);
    for (int i = 0; i < 10000; ++i) {
        out << "INSERT INTO students VALUES (" << i << ", 'user_" << i << "');\n";
    }
    out.close();

    std::cout << "[RECOVERY] Timing Crash Recovery WAL replay of 10,000 entries...\n";
    Catalog rec_catalog;
    Table* rec_table = rec_catalog.createTable("students", {{"id", ValueType::Int}, {"name", ValueType::Text}}, 0);

    auto rec_start = Clock::now();
    std::ifstream in(wal_file);
    std::string log_line;
    int replayed = 0;
    while (std::getline(in, log_line)) {
        size_t open_paren = log_line.find('(');
        size_t close_paren = log_line.find(')');
        if (open_paren != std::string::npos && close_paren != std::string::npos) {
            std::string vals_str = log_line.substr(open_paren + 1, close_paren - open_paren - 1);
            size_t comma = vals_str.find(',');
            if (comma != std::string::npos) {
                int64_t id = std::stoll(vals_str.substr(0, comma));
                std::string name = vals_str.substr(comma + 1);
                size_t nf = name.find_first_not_of(" \t'");
                size_t nl = name.find_last_not_of(" \t'");
                name = name.substr(nf, nl - nf + 1);

                Tuple row = { Value::Int(id), Value::Text(name) };
                ExecContext rec_ctx;
                Insert ins_op(rec_table, row, rec_ctx);
                execute(ins_op);
                replayed++;
            }
        }
    }
    in.close();
    auto rec_end = Clock::now();
    std::filesystem::remove(wal_file);

    double rec_ms = std::chrono::duration<double, std::milli>(rec_end - rec_start).count();
    double rec_speed = replayed / (rec_ms / 1000.0);

    std::cout << "  * WAL Crash Recovery:\n";
    std::cout << "    - Replayed entries: " << replayed << "\n";
    std::cout << "    - Total recovery time: " << rec_ms << " ms\n";
    std::cout << "    - Recovery throughput: " << static_cast<long>(rec_speed) << " rows/sec\n\n";

    std::cout << "Benchmark suite execution completed.\n";
    return 0;
}
