#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#include "engine/database.h"
#include "executor/execution_metrics.h"

namespace {

constexpr char kQuery[] = "SELECT * FROM users WHERE age = 30;";

struct BenchConfig {
    int warmup_runs = 2;
    int measure_runs = 10;
    std::vector<int> scales{500};
};

struct ModeResult {
    std::string mode;
    double lat_min_ms = 0.0;
    double lat_avg_ms = 0.0;
    double lat_p50_ms = 0.0;
    double throughput_qps = 0.0;
    minidb::ExecutionMetrics exec{};
    std::size_t buffer_hits = 0;
    std::size_t buffer_misses = 0;
    double buffer_hit_rate = 0.0;
    double est_bytes_read = 0.0;
};

BenchConfig ParseArgs(int argc, char* argv[]) {
    BenchConfig cfg;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--full") {
            cfg.warmup_runs = 2;
            cfg.measure_runs = 10;
            cfg.scales = {200, 500, 1000};
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: benchmark [--full]\n"
                      << "  default: quick run (~1s, 500 rows, 10 iterations)\n"
                      << "  --full:  extended run (~45s, 200/500/1000 rows, 10 iterations)\n";
            std::exit(0);
        }
    }
    return cfg;
}

double Percentile(std::vector<double> values, double pct) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    std::size_t idx = static_cast<std::size_t>(
        std::min(values.size() - 1, static_cast<std::size_t>(pct * values.size())));
    return values[idx];
}

void LoadUsers(minidb::Database& db, int rows) {
    std::cerr << "  Loading " << rows << " rows...\n";
    db.ExecuteSql("CREATE TABLE users (id INT PRIMARY KEY, name STRING, age INT);");
    db.ExecuteSql("BEGIN;");
    for (int i = 0; i < rows; ++i) {
        db.ExecuteSql("INSERT INTO users (id, name, age) VALUES (" + std::to_string(i) +
                        ", 'user" + std::to_string(i) + "', " + std::to_string(20 + (i % 50)) +
                        ");");
    }
    db.ExecuteSql("COMMIT;");
}

ModeResult BenchmarkMode(const std::string& db_path, bool batch_mode, int rows,
                         const BenchConfig& cfg) {
    std::filesystem::remove_all(db_path);
    minidb::Database db(db_path);
    LoadUsers(db, rows);
    db.SetBatchMode(batch_mode);

    for (int i = 0; i < cfg.warmup_runs; ++i) {
        db.ExecuteSql(kQuery);
    }

    db.ResetBufferPoolCounters();
    minidb::ExecutionMetricsHolder::Reset();

    std::vector<double> latencies;
    latencies.reserve(static_cast<std::size_t>(cfg.measure_runs));
    minidb::ExecutionMetrics total_exec{};
    auto total_start = std::chrono::steady_clock::now();
    for (int i = 0; i < cfg.measure_runs; ++i) {
        auto start = std::chrono::steady_clock::now();
        db.ExecuteSql(kQuery);
        auto end = std::chrono::steady_clock::now();
        latencies.push_back(
            std::chrono::duration<double, std::milli>(end - start).count());
        auto m = db.GetExecutionMetrics();
        total_exec.tuples_scanned = m.tuples_scanned;
        total_exec.tuples_output += m.tuples_output;
        total_exec.batches_processed += m.batches_processed;
        total_exec.columnar_vector_bytes += m.columnar_vector_bytes;
        if (m.used_columnar_filter) total_exec.used_columnar_filter = true;
    }
    auto total_end = std::chrono::steady_clock::now();
    double total_sec = std::chrono::duration<double>(total_end - total_start).count();

    ModeResult result;
    result.mode = batch_mode ? "Batch+Columnar" : "Row Store";
    result.lat_min_ms = *std::min_element(latencies.begin(), latencies.end());
    result.lat_avg_ms =
        std::accumulate(latencies.begin(), latencies.end(), 0.0) /
        static_cast<double>(latencies.size());
    result.lat_p50_ms = Percentile(latencies, 0.50);
    result.throughput_qps = total_sec > 0.0 ? static_cast<double>(cfg.measure_runs) / total_sec : 0.0;
    result.exec = total_exec;
    result.exec.tuples_output /= static_cast<std::size_t>(cfg.measure_runs);
    result.exec.batches_processed /= static_cast<std::size_t>(cfg.measure_runs);
    result.exec.columnar_vector_bytes /= static_cast<std::size_t>(cfg.measure_runs);
    result.buffer_hits = db.buffer_pool()->HitCount();
    result.buffer_misses = db.buffer_pool()->MissCount();
    const std::size_t buffer_access = result.buffer_hits + result.buffer_misses;
    result.buffer_hit_rate =
        buffer_access == 0 ? 0.0
                           : (100.0 * static_cast<double>(result.buffer_hits) /
                              static_cast<double>(buffer_access));
    result.est_bytes_read =
        static_cast<double>(result.exec.tuples_scanned * 64 + result.exec.columnar_vector_bytes);
    return result;
}

void PrintMode(const ModeResult& r) {
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "  " << std::left << std::setw(16) << r.mode << "  "
              << "lat_min=" << r.lat_min_ms << " ms  "
              << "lat_avg=" << r.lat_avg_ms << " ms  "
              << "lat_p50=" << r.lat_p50_ms << " ms  "
              << "qps=" << r.throughput_qps << '\n';
    std::cout << "    tuples_scanned=" << r.exec.tuples_scanned
              << "  tuples_output=" << r.exec.tuples_output
              << "  batches=" << r.exec.batches_processed
              << "  columnar=" << (r.exec.used_columnar_filter ? "yes" : "no")
              << "  columnar_bytes=" << r.exec.columnar_vector_bytes
              << "  buffer_hit_rate=" << r.buffer_hit_rate << '%'
              << "  est_bytes=" << static_cast<std::size_t>(r.est_bytes_read) << '\n';
}

void RunScale(int rows, const BenchConfig& cfg) {
    std::cerr << "Benchmarking scale " << rows << " rows...\n";
    const std::string base = "benchmark_data_" + std::to_string(rows);
    ModeResult row = BenchmarkMode(base + "_row", false, rows, cfg);
    ModeResult batch = BenchmarkMode(base + "_batch", true, rows, cfg);

    std::cout << "\n--- Scale: " << rows << " rows | " << cfg.measure_runs << " iterations ---\n";
    PrintMode(row);
    PrintMode(batch);

    double lat_speedup = batch.lat_avg_ms > 0.0 ? row.lat_avg_ms / batch.lat_avg_ms : 0.0;
    double qps_speedup = row.throughput_qps > 0.0 ? batch.throughput_qps / row.throughput_qps : 0.0;
    std::cout << "  Speedup (latency avg): " << lat_speedup << "x\n";
    std::cout << "  Speedup (throughput):  " << qps_speedup << "x\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        BenchConfig cfg = ParseArgs(argc, argv);
        std::cout << "Track A Benchmark — Performance Extension\n";
        std::cout << "Query: " << kQuery << '\n';
        std::cout << "Warmup runs: " << cfg.warmup_runs << " | Measured runs: " << cfg.measure_runs
                  << '\n';
        std::cout << "Metrics: latency (min/avg/p50), throughput (QPS), tuples scanned, "
                     "columnar vector bytes, buffer hit rate\n";

        for (int rows : cfg.scales) {
            RunScale(rows, cfg);
        }

        std::cout << "\nTrack A Benchmark complete.\n";
    } catch (const std::exception& ex) {
        std::cerr << "Benchmark error: " << ex.what() << '\n';
        return 1;
    }
    return 0;
}
