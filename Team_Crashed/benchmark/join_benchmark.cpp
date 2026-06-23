#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "catalog/catalog_manager.h"
#include "common/status.h"
#include "executor/query_engine.h"
#include "index/index_manager.h"
#include "recovery/recovery_manager.h"
#include "recovery/wal.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"
#include "transaction/transaction_manager.h"

using namespace minidb;

static bool openStack(std::unique_ptr<storage::DiskManager>& dm,
                      std::unique_ptr<storage::BufferPool>& bp,
                      std::unique_ptr<catalog::CatalogManager>& cat,
                      std::unique_ptr<index::IndexManager>& idx,
                      std::unique_ptr<transaction::TransactionManager>& txn,
                      std::unique_ptr<recovery::WAL>& wal,
                      std::unique_ptr<recovery::RecoveryManager>& rec) {
    try {
        dm = std::make_unique<storage::DiskManager>("data/bench_join/minidb.db");
        bp = std::make_unique<storage::BufferPool>(dm.get(), 128);
        cat = std::make_unique<catalog::CatalogManager>(dm.get());
        Status cs = cat->load();
        if (cs != Status::OK && cs != Status::UNIMPLEMENTED) return false;
        idx = std::make_unique<index::IndexManager>(bp.get(), cat.get());
        txn = std::make_unique<transaction::TransactionManager>();
        wal = std::make_unique<recovery::WAL>("data/bench_join/minidb.wal");
        rec = std::make_unique<recovery::RecoveryManager>(
            wal.get(), bp.get(), cat.get(), idx.get(), txn.get());
        Status rs = rec->runAtStartup();
        return rs == Status::OK || rs == Status::UNIMPLEMENTED;
    } catch (...) {
        return false;
    }
}

static void emitRow(int scale, std::size_t rows, double elapsedMs) {
    const double rowsPerSec = elapsedMs > 0.0 ? rows * 1000.0 / elapsedMs : 0.0;
    char buf[256];
    std::snprintf(buf, sizeof(buf), "join,%d,%zu,%.3f,%.2f",
                  scale, rows, elapsedMs, rowsPerSec);
    std::printf("%s\n", buf);
    std::fprintf(stderr,
                 "[join_benchmark] scale=%d rows=%zu elapsed=%.3f ms rows_per_sec=%.0f\n",
                 scale, rows, elapsedMs, rowsPerSec);

    std::error_code ec;
    std::filesystem::create_directories("benchmark_results", ec);
    std::ofstream out("benchmark_results/join.csv", std::ios::trunc);
    if (out) {
        out << "op,scale,rows,elapsed_ms,rows_per_sec\n";
        out << buf << "\n";
    }
}

int main(int argc, char** argv) {
    int scale = 100;
    if (argc > 1) {
        scale = std::atoi(argv[1]);
        if (scale <= 0) scale = 100;
    }

    std::error_code ec;
    std::filesystem::remove_all("data/bench_join", ec);
    std::filesystem::create_directories("data/bench_join", ec);

    std::unique_ptr<storage::DiskManager> dm;
    std::unique_ptr<storage::BufferPool> bp;
    std::unique_ptr<catalog::CatalogManager> cat;
    std::unique_ptr<index::IndexManager> idx;
    std::unique_ptr<transaction::TransactionManager> txn;
    std::unique_ptr<recovery::WAL> wal;
    std::unique_ptr<recovery::RecoveryManager> rec;

    if (!openStack(dm, bp, cat, idx, txn, wal, rec)) {
        std::fprintf(stderr, "join_benchmark: failed to open benchmark DB\n");
        return 1;
    }

    executor::QueryEngine qe(bp.get(), cat.get(), idx.get(), txn.get(), rec.get());
    Status s = qe.executeUpdate(
        "CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(16), age INT);");
    if (s != Status::OK) return 1;
    s = qe.executeUpdate(
        "CREATE TABLE orders (id INT PRIMARY KEY, user_id INT, total INT);");
    if (s != Status::OK) return 1;

    for (int i = 1; i <= scale; ++i) {
        char sql[192];
        std::snprintf(sql, sizeof(sql),
                      "INSERT INTO users VALUES (%d, 'u_%d', %d);",
                      i, i, 20 + (i % 50));
        if (qe.executeUpdate(sql) != Status::OK) return 1;
        std::snprintf(sql, sizeof(sql),
                      "INSERT INTO orders VALUES (%d, %d, %d);",
                      i, i, 50 + (i % 200));
        if (qe.executeUpdate(sql) != Status::OK) return 1;
    }

    const auto t0 = std::chrono::high_resolution_clock::now();
    auto rows = qe.execute(
        "SELECT users.id, orders.total "
        "FROM users JOIN orders ON users.id = orders.user_id "
        "WHERE orders.total >= 75;");
    const auto t1 = std::chrono::high_resolution_clock::now();
    const double elapsedMs =
        std::chrono::duration<double, std::milli>(t1 - t0).count();

    emitRow(scale, rows.size(), elapsedMs);
    return 0;
}
