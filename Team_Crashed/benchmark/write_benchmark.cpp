#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "catalog/catalog_manager.h"
#include "common/record_id.h"
#include "common/status.h"
#include "common/types.h"
#include "executor/query_engine.h"
#include "index/index_manager.h"
#include "recovery/recovery_manager.h"
#include "recovery/wal.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"
#include "transaction/transaction_manager.h"

using namespace minidb;

static bool openStack(const std::string& dbPath,
                      const std::string& walPath,
                      std::unique_ptr<storage::DiskManager>& dm,
                      std::unique_ptr<storage::BufferPool>& bp,
                      std::unique_ptr<catalog::CatalogManager>& cat,
                      std::unique_ptr<index::IndexManager>& idx,
                      std::unique_ptr<transaction::TransactionManager>& txn,
                      std::unique_ptr<recovery::WAL>& wal,
                      std::unique_ptr<recovery::RecoveryManager>& rec) {
    try {
        dm = std::make_unique<storage::DiskManager>(dbPath);
        bp = std::make_unique<storage::BufferPool>(dm.get(), 64);
        cat = std::make_unique<catalog::CatalogManager>(dm.get());
        Status cs = cat->load();
        if (cs != Status::OK && cs != Status::UNIMPLEMENTED) return false;
        idx = std::make_unique<index::IndexManager>(bp.get(), cat.get());
        txn = std::make_unique<transaction::TransactionManager>();
        wal = std::make_unique<recovery::WAL>(walPath);
        rec = std::make_unique<recovery::RecoveryManager>(
            wal.get(), bp.get(), cat.get(), idx.get(), txn.get());
        Status rs = rec->runAtStartup();
        return rs == Status::OK || rs == Status::UNIMPLEMENTED;
    } catch (...) {
        return false;
    }
}

static double runInsertBatch(executor::QueryEngine& qe,
                             transaction::TransactionManager* txn,
                             int n,
                             bool withLock) {
    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 1; i <= n; ++i) {
        char sql[160];
        std::snprintf(sql, sizeof(sql),
                      "INSERT INTO bench_t VALUES (%d, 'row_%d');", i, i);
        if (withLock && txn != nullptr) {
            TransactionId tid = txn->begin();
            RecordId rid = makeRid(static_cast<PageId>(i & 0xFFFF), 0);
            (void)txn->lockManager().acquireExclusive(tid, rid);
            (void)qe.executeUpdate(sql);
            (void)txn->commit(tid);
        } else {
            (void)qe.executeUpdate(sql);
        }
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

static double runPhase(int n, bool withLock) {
    std::error_code ec;
    std::filesystem::remove_all("data/bench_write", ec);
    std::filesystem::create_directories("data/bench_write", ec);

    std::unique_ptr<storage::DiskManager> dm;
    std::unique_ptr<storage::BufferPool> bp;
    std::unique_ptr<catalog::CatalogManager> cat;
    std::unique_ptr<index::IndexManager> idx;
    std::unique_ptr<transaction::TransactionManager> txn;
    std::unique_ptr<recovery::WAL> wal;
    std::unique_ptr<recovery::RecoveryManager> rec;

    if (!openStack("data/bench_write/minidb.db",
                   "data/bench_write/minidb.wal",
                   dm, bp, cat, idx, txn, wal, rec)) {
        return -1.0;
    }

    executor::QueryEngine qe(bp.get(), cat.get(), idx.get(), txn.get(), rec.get());
    Status s = qe.executeUpdate(
        "CREATE TABLE bench_t (id INT PRIMARY KEY, payload VARCHAR(32));");
    if (s != Status::OK) return -1.0;

    const double ms = runInsertBatch(qe, txn.get(), n, withLock);
    (void)cat->flush();
    (void)bp->flushAll();
    dm->flush();
    return ms;
}

static void emitRow(int n, double withMs, double withoutMs) {
    const double speedup = (withoutMs > 0.0) ? (withMs / withoutMs) : 0.0;
    char buf[256];
    std::snprintf(buf, sizeof(buf), "insert,%d,%.3f,%.3f,%.2f",
                  n, withMs, withoutMs, speedup);
    std::printf("%s\n", buf);
    std::fprintf(stderr,
                 "[write_benchmark] N=%d with_lock=%.3f ms without_lock=%.3f ms speedup=%.2fx\n",
                 n, withMs, withoutMs, speedup);

    std::error_code ec;
    std::filesystem::create_directories("benchmark_results", ec);
    std::ofstream out("benchmark_results/write.csv", std::ios::trunc);
    if (out) {
        out << "op,n,elapsed_ms_with_lock,elapsed_ms_without_lock,speedup\n";
        out << buf << "\n";
    }
}

int main(int argc, char** argv) {
    int n = 1000;
    if (argc > 1) {
        n = std::atoi(argv[1]);
        if (n <= 0) n = 1000;
    }

    const double withMs = runPhase(n, /*withLock=*/true);
    const double withoutMs = runPhase(n, /*withLock=*/false);
    if (withMs < 0.0 || withoutMs < 0.0) {
        std::fprintf(stderr, "write_benchmark: setup or insert phase failed\n");
        return 1;
    }

    emitRow(n, withMs, withoutMs);
    return 0;
}
