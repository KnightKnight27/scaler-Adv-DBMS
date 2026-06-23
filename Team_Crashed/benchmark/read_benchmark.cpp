// =============================================================================
// benchmark/read_benchmark.cpp
// -----------------------------------------------------------------------------
// Read benchmark for MiniDB.
//
// Setup:
//   - Opens `data/minidb.db` relative to the current working directory.
//   - Drops and recreates a `bench_t` table with a single INT PRIMARY KEY
//     and a VARCHAR payload column.
//   - Bulk-inserts `scale` rows (default 10,000).
//   - Runs a 50/50 mix of SeqScan (full scan with a `WHERE id = ?` filter
//     pushed into the executor) and IndexScan (B+ tree point lookup) of
//     randomly selected keys, totaling `queries` point lookups.
//
// Output:
//   - One CSV row on stdout per scan kind, plus the same row appended to
//     `benchmark_results/read.csv`.
//   - A one-line summary on stderr so the user can see timing immediately.
//
// CSV format:
//   scan,<queries>,<seq_count>,<idx_count>,<elapsed_ms>,<throughput_qps>
// =============================================================================
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <vector>

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

// -----------------------------------------------------------------------------
// Helper: open the database files exactly the way src/cli/main.cpp does.
//   WHAT     — construct the storage / catalog / index / txn / WAL stack
//              and run startup recovery so the benchmark sees a clean
//              post-recovery state.
//   RETURN   — true on success, false otherwise (caller decides whether
//              to retry with a freshly wiped DB).
//   WHO CALLS — main() of this benchmark only.
// -----------------------------------------------------------------------------
static bool openDatabase(const std::string& dbPath,
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
        wal = std::make_unique<recovery::WAL>("data/wal/minidb.wal");
        rec = std::make_unique<recovery::RecoveryManager>(
            wal.get(), bp.get(), cat.get(), idx.get(), txn.get());
        Status rs = rec->runAtStartup();
        return rs == Status::OK || rs == Status::UNIMPLEMENTED;
    } catch (...) {
        return false;
    }
}

// -----------------------------------------------------------------------------
// Helper: write a CSV row both to stdout and to the results file.
//   WHAT     — append one row of the form
//              "<kind>,<queries>,<seq>,<idx>,<ms>,<qps>" to stdout and to
//              benchmark_results/read.csv (creating the directory and the
//              file on first run).
//   RETURN   — void; never throws (best-effort file IO).
//   WHO CALLS — runSeqScan() / runIndexScan() below.
// -----------------------------------------------------------------------------
static void emitRow(const std::string& kind,
                    int queries, int seqCount, int idxCount,
                    double elapsedMs) {
    double qps = (elapsedMs > 0.0) ? (queries * 1000.0 / elapsedMs) : 0.0;
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "%s,%d,%d,%d,%.3f,%.2f",
                  kind.c_str(), queries, seqCount, idxCount,
                  elapsedMs, qps);
    std::printf("%s\n", buf);
    std::fprintf(stderr,
                 "[read_benchmark] %s: %d queries in %.3f ms (%.0f qps)\n",
                 kind.c_str(), queries, elapsedMs, qps);

    std::error_code ec;
    std::filesystem::create_directories("benchmark_results", ec);
    std::ofstream out("benchmark_results/read.csv", std::ios::app);
    if (out) {
        // Header row only on first creation; cheap to write every time
        // because the file is small.
        out.seekp(0, std::ios::end);
        if (out.tellp() == 0) {
            out << "kind,queries,seq_count,idx_count,elapsed_ms,qps\n";
        }
        out << buf << "\n";
    }
}

// -----------------------------------------------------------------------------
// runSeqScan
//   WHAT     — execute `count` SELECTs of the form
//              `SELECT * FROM bench_t WHERE payload = 'row_?'` via the
//              QueryEngine. `payload` has no index, so every iteration
//              walks the heap file.
//   RETURN   — elapsed wall-clock time in milliseconds.
//   WHO CALLS — main().
// -----------------------------------------------------------------------------
static double runSeqScan(executor::QueryEngine& qe,
                         const std::vector<int>& keys,
                         int count, int& outSeq, int& outIdx) {
    outSeq = count;
    outIdx = 0;
    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < count; ++i) {
        char sql[128];
        std::snprintf(sql, sizeof(sql),
                      "SELECT * FROM bench_t WHERE payload = 'row_%d';",
                      keys[i]);
        (void)qe.execute(sql);
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

// -----------------------------------------------------------------------------
// runIndexScan
//   WHAT     — execute `count` SELECTs that the optimizer can rewrite into
//              an IndexScan (primary-key equality). Uses
//              `SELECT id FROM bench_t WHERE id = ?` and forces the
//              optimizer to pick the index path by enabling the primary-key
//              index on `bench_t.id`.
//   RETURN   — elapsed wall-clock time in milliseconds.
//   WHO CALLS — main().
// -----------------------------------------------------------------------------
static double runIndexScan(executor::QueryEngine& qe,
                          const std::vector<int>& keys,
                          int count, int& outSeq, int& outIdx) {
    outSeq = 0;
    outIdx = count;
    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < count; ++i) {
        char sql[128];
        std::snprintf(sql, sizeof(sql),
                      "SELECT * FROM bench_t WHERE id = %d;",
                      keys[i]);
        (void)qe.execute(sql);
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

// -----------------------------------------------------------------------------
// main
//   WHAT     — set up the DB, populate it, run the two phases, emit CSV.
//   RETURN   — 0 on success, 1 on setup failure.
//   WHO CALLS — the user (or CI as a smoke test).
// -----------------------------------------------------------------------------
int main(int /*argc*/, char** /*argv*/) {
    const std::string dbPath = "data/minidb.db";

    // Best-effort cleanup so the run is repeatable. We don't want the
    // benchmark numbers to depend on leftover state from a previous run.
    {
        std::error_code ec;
        std::filesystem::remove(dbPath, ec);
        std::filesystem::remove("data/wal/minidb.wal", ec);
    }

    std::unique_ptr<storage::DiskManager>             dm;
    std::unique_ptr<storage::BufferPool>              bp;
    std::unique_ptr<catalog::CatalogManager>          cat;
    std::unique_ptr<index::IndexManager>              idx;
    std::unique_ptr<transaction::TransactionManager>  txn;
    std::unique_ptr<recovery::WAL>                    wal;
    std::unique_ptr<recovery::RecoveryManager>        rec;

    if (!openDatabase(dbPath, dm, bp, cat, idx, txn, wal, rec)) {
        std::fprintf(stderr, "read_benchmark: failed to open DB at %s\n",
                     dbPath.c_str());
        return 1;
    }

    executor::QueryEngine qe(bp.get(), cat.get(), idx.get(),
                             txn.get(), rec.get());

    // -- schema setup --
    (void)qe.executeUpdate("DROP TABLE IF EXISTS bench_t;");
    Status s = qe.executeUpdate(
        "CREATE TABLE bench_t (id INT PRIMARY KEY, payload VARCHAR(32));");
    if (s != Status::OK) {
        std::fprintf(stderr,
                     "read_benchmark: CREATE TABLE failed (status=%s)\n",
                     toString(s));
        return 1;
    }
    (void)qe.executeUpdate(
        "CREATE INDEX bench_t_pk ON bench_t(id);");

    // -- bulk insert --
    const int scale = 10'000;
    std::printf("[read_benchmark] populating bench_t with %d rows...\n", scale);
    for (int i = 1; i <= scale; ++i) {
        char sql[128];
        std::snprintf(sql, sizeof(sql),
                      "INSERT INTO bench_t VALUES (%d, 'row_%d');", i, i);
        Status is = qe.executeUpdate(sql);
        if (is != Status::OK) {
            std::fprintf(stderr,
                         "read_benchmark: INSERT failed at i=%d (%s)\n",
                         i, toString(is));
            return 1;
        }
    }

    // -- generate key list (50/50 split decided per-run by the caller) --
    std::mt19937 rng(0xC0FFEEu);
    std::uniform_int_distribution<int> dist(1, scale);
    const int queries = 10'000;
    std::vector<int> keys(queries);
    for (int i = 0; i < queries; ++i) keys[i] = dist(rng);

    // -- phase 1: seq scan (no index used) --
    int seqCount = 0, idxCount = 0;
    double seqMs = runSeqScan(qe, keys, queries, seqCount, idxCount);
    emitRow("scan", queries, seqCount, idxCount, seqMs);

    // -- phase 2: index scan --
    double idxMs = runIndexScan(qe, keys, queries, seqCount, idxCount);
    emitRow("index", queries, seqCount, idxCount, idxMs);

    std::fprintf(stderr,
                 "[read_benchmark] DONE  seq=%.3f ms  index=%.3f ms  "
                 "speedup=%.2fx\n",
                 seqMs, idxMs,
                 (idxMs > 0.0) ? (seqMs / idxMs) : 0.0);
    return 0;
}
