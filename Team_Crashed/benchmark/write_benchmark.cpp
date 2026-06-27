// =============================================================================
// benchmark/write_benchmark.cpp
// -----------------------------------------------------------------------------
// Concurrency-control benchmark: 2PL (strict two-phase locking) vs MVCC
// (snapshot isolation) on a contended read/write workload.
//
// WHY this benchmark exists
// --------------------------
// The earlier version of this benchmark measured single-threaded insert
// throughput with and without acquiring a lock per insert. That is not a
// meaningful 2PL-vs-MVCC comparison: with one thread there is no contention,
// so MVCC's whole advantage (non-blocking reads under concurrent writers)
// never appears. This rewrite runs a REAL concurrent mixed workload and
// measures the thing MVCC is actually for: reader throughput while writers
// are mutating the same table.
//
// WORKLOAD
// --------
//   * A shared table of N rows is pre-populated (all committed).
//   * R reader threads and W writer threads run concurrently for T seconds.
//   * Each reader does: BEGIN, scan every row, COMMIT. It counts the rows it
//     successfully observed under the protocol's visibility rules.
//   * Each writer does: BEGIN, update the next row in its private round-robin
//     partition, COMMIT. Writers are partitioned so no two writers touch the
//     same row concurrently (this keeps the MVCC version chain consistent and
//     isolates the measurement to read/write, not write/write, contention;
//     write/write conflict is exercised by tests/transaction/transaction_test.cpp).
//
// THE TWO PROTOCOLS
// -----------------
//   2PL  — readers acquire an S lock on every row they read and hold it until
//          COMMIT; writers acquire an X lock on the row they update. A reader
//          that reaches a row an active writer holds an X lock on BLOCKS until
//          that writer commits (and vice versa). This is the read/write
//          blocking that strict 2PL is criticised for.
//   MVCC — readers take NO locks; they read each row's newest version visible
//          to their snapshot (TransactionManager::isVisible), so a row an
//          in-flight writer is mutating is simply seen at its old, committed
//          value. Readers therefore never block on writers. Writers record
//          their write-set for first-updater-wins conflict detection at commit.
//
// Both phases use the REAL TransactionManager + LockManager (the same code
// the executor path uses after the MVCC/2PL wiring). The shared row store is
// guarded by a single mutex (only the physical map access is serialised; the
// CC protocol determines whether a thread blocks, and that blocking happens
// inside the lock manager, not under the store mutex).
//
// METRIC: operations per second (reads + writes) over the run window, plus
// reads/s and writes/s separately, and the number of deadlocks/aborts
// observed (2PL only, in principle). MVCC is expected to sustain markedly
// higher reader throughput under contention because readers never stall.
// =============================================================================
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <random>
#include <thread>
#include <unordered_map>
#include <vector>

#include "common/status.h"
#include "common/types.h"
#include "transaction/transaction_manager.h"

using namespace minidb;

namespace {

// Concurrency-control protocol under test (local to this benchmark so it
// doesn't drag in the executor headers).
enum class CC { TwoPL, MVCC };

// A single row version: who created it, who deleted it (0 == live), and its
// integer payload. created == 0 marks the initial pre-populated, always-
// committed version (visible to every snapshot).
struct Version {
    TransactionId created = 0;
    TransactionId deleted = 0;
    int64_t       value   = 0;
};

// The shared table. One version chain per RecordId (we use rid == 1..N).
// All physical access goes through `mu`.
struct Table {
    std::mutex                                          mu;
    std::unordered_map<RecordId, std::vector<Version>>  rows;
};

// Pre-populate N committed rows with value == rid.
void populate(Table& t, int N) {
    std::lock_guard<std::mutex> lk(t.mu);
    for (int i = 1; i <= N; ++i) {
        Version v;
        v.created = 0;            // initial committed version
        v.deleted = 0;
        v.value   = i;
        t.rows[static_cast<RecordId>(i)].push_back(v);
    }
}

// Find the newest version of `rid` visible to `reader` (nullptr => see all).
// Walks newest-to-oldest and returns the first visible version's value, or
// 0 if none is visible. Caller must hold t.mu.
int64_t readVisible(Table& t, RecordId rid,
                    transaction::TransactionManager* tm,
                    const transaction::Transaction* reader) {
    auto it = t.rows.find(rid);
    if (it == t.rows.end()) return 0;
    for (auto vIt = it->second.rbegin(); vIt != it->second.rend(); ++vIt) {
        if (tm->isVisible(vIt->created, vIt->deleted, *reader)) {
            return vIt->value;
        }
    }
    return 0;
}

struct Counters {
    std::atomic<int64_t> reads   {0};
    std::atomic<int64_t> writes  {0};
    std::atomic<int64_t> aborts  {0};   // 2PL: DEADLOCK retries; MVCC: conflicts
};

// ---- 2PL reader: S-lock every row, read, commit (release all at commit) ----
void reader2PL(Table& t, transaction::TransactionManager& tm,
               int N, std::atomic<bool>& stop, Counters& c) {
    while (!stop.load(std::memory_order_relaxed)) {
        TransactionId tid = tm.begin();
        bool deadlocked = false;
        for (int i = 1; i <= N; ++i) {
            RecordId rid = static_cast<RecordId>(i);
            Status s = tm.lockManager().acquireShared(tid, rid);
            if (s == Status::DEADLOCK) { deadlocked = true; break; }
            int64_t val;
            { std::lock_guard<std::mutex> lk(t.mu); val = t.rows[rid].back().value; }
            (void)val;
            c.reads.fetch_add(1, std::memory_order_relaxed);
            if (stop.load(std::memory_order_relaxed)) break;
        }
        if (deadlocked) {
            c.aborts.fetch_add(1, std::memory_order_relaxed);
            (void)tm.abort(tid);
        } else {
            (void)tm.commit(tid);   // strict 2PL: releases every S lock here
        }
    }
}

// ---- 2PL writer: X-lock my partition row, bump value in place, commit ----
void writer2PL(Table& t, transaction::TransactionManager& tm,
               int N, int W, int id, std::atomic<bool>& stop, Counters& c) {
    int next = id + 1;   // round-robin start within this writer's partition
    while (!stop.load(std::memory_order_relaxed)) {
        int row = next;
        next += W;
        if (next > N) next = id + 1;
        if (row > N) row = (row % N) + 1;
        RecordId rid = static_cast<RecordId>(row);
        TransactionId tid = tm.begin();
        Status s = tm.lockManager().acquireExclusive(tid, rid);
        if (s == Status::DEADLOCK) {
            c.aborts.fetch_add(1, std::memory_order_relaxed);
            (void)tm.abort(tid);
            continue;
        }
        { std::lock_guard<std::mutex> lk(t.mu); t.rows[rid].back().value += 1; }
        c.writes.fetch_add(1, std::memory_order_relaxed);
        (void)tm.commit(tid);
        if (stop.load(std::memory_order_relaxed)) break;
    }
}

// ---- MVCC reader: no locks; read newest version visible to my snapshot ----
void readerMVCC(Table& t, transaction::TransactionManager& tm,
                int N, std::atomic<bool>& stop, Counters& c) {
    while (!stop.load(std::memory_order_relaxed)) {
        TransactionId tid = tm.begin();
        const transaction::Transaction* reader = tm.getTransaction(tid);
        for (int i = 1; i <= N; ++i) {
            int64_t val;
            { std::lock_guard<std::mutex> lk(t.mu);
              val = readVisible(t, static_cast<RecordId>(i), &tm, reader); }
            (void)val;
            c.reads.fetch_add(1, std::memory_order_relaxed);
            if (stop.load(std::memory_order_relaxed)) break;
        }
        (void)tm.commit(tid);
    }
}

// ---- MVCC writer: record write-set, append a new version, commit ----
// (partitioned so writers never collide on the same row concurrently =>
//  the version chain stays consistent and we measure read/write, not
//  write/write, contention.)
void writerMVCC(Table& t, transaction::TransactionManager& tm,
               int N, int W, int id, std::atomic<bool>& stop, Counters& c) {
    int next = id + 1;
    while (!stop.load(std::memory_order_relaxed)) {
        int row = next;
        next += W;
        if (next > N) next = id + 1;
        if (row > N) row = (row % N) + 1;
        RecordId rid = static_cast<RecordId>(row);

        TransactionId tid = tm.begin();
        tm.recordWrite(tid, rid);

        // Append a new version stamped with this txn; mark the previous live
        // version as deleted-by-this-txn. (Done under the store mutex.)
        { std::lock_guard<std::mutex> lk(t.mu);
          auto& chain = t.rows[rid];
          Version nv;
          nv.created = tid;
          nv.deleted = 0;
          nv.value   = chain.empty() ? 0 : chain.back().value + 1;
          if (!chain.empty() && chain.back().deleted == 0) chain.back().deleted = tid;
          chain.push_back(nv);
        }

        Status s = tm.commit(tid);
        if (s == Status::TXN_CONFLICT) {
            // First-updater-wins lost: undo our version append.
            c.aborts.fetch_add(1, std::memory_order_relaxed);
            { std::lock_guard<std::mutex> lk(t.mu);
              auto& chain = t.rows[rid];
              if (!chain.empty() && chain.back().created == tid) {
                chain.pop_back();
                if (!chain.empty() && chain.back().deleted == tid) chain.back().deleted = 0;
              }
            }
            (void)tm.abort(tid);
            continue;
        }
        c.writes.fetch_add(1, std::memory_order_relaxed);
        if (stop.load(std::memory_order_relaxed)) break;
    }
}

struct PhaseResult {
    double seconds;
    int64_t reads;
    int64_t writes;
    int64_t aborts;
    double readsPerSec()  const { return seconds > 0 ? reads  / seconds : 0; }
    double writesPerSec() const { return seconds > 0 ? writes / seconds : 0; }
    double opsPerSec()    const { return readsPerSec() + writesPerSec(); }
};

PhaseResult runPhase(CC mode, int N, int R, int W, double seconds) {
    Table t;
    populate(t, N);
    transaction::TransactionManager tm;

    std::atomic<bool> stop{false};
    Counters c;

    std::vector<std::thread> threads;
    for (int r = 0; r < R; ++r)
        threads.emplace_back(mode == CC::TwoPL ? reader2PL : readerMVCC,
                             std::ref(t), std::ref(tm), N, std::ref(stop), std::ref(c));
    for (int w = 0; w < W; ++w)
        threads.emplace_back(mode == CC::TwoPL ? writer2PL : writerMVCC,
                             std::ref(t), std::ref(tm), N, W, w + 1, std::ref(stop), std::ref(c));

    auto t0 = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(
        std::chrono::milliseconds(static_cast<int>(seconds * 1000)));
    stop.store(true);
    for (auto& th : threads) th.join();
    auto t1 = std::chrono::steady_clock::now();

    PhaseResult res;
    res.seconds = std::chrono::duration<double>(t1 - t0).count();
    res.reads   = c.reads.load();
    res.writes  = c.writes.load();
    res.aborts  = c.aborts.load();
    return res;
}

} // namespace

int main(int argc, char** argv) {
    int    N = 500;     // rows
    int    R = 4;       // reader threads
    int    W = 2;       // writer threads
    double T = 1.0;     // seconds per phase
    if (argc > 1) N = std::atoi(argv[1]);
    if (argc > 2) R = std::atoi(argv[2]);
    if (argc > 3) W = std::atoi(argv[3]);
    if (argc > 4) T = std::atof(argv[4]);
    if (N <= 0) N = 500;
    if (R <= 0) R = 4;
    if (W <= 0) W = 2;
    if (T <= 0) T = 1.0;

    std::printf("[write_benchmark] N=%d readers=%d writers=%d seconds=%.2f\n",
                N, R, W, T);

    PhaseResult twoPL = runPhase(CC::TwoPL, N, R, W, T);
    PhaseResult mvcc  = runPhase(CC::MVCC,   N, R, W, T);

    std::printf("\n=== 2PL (strict two-phase locking) ===\n");
    std::printf("  reads/s : %.0f   writes/s : %.0f   ops/s : %.0f\n",
                twoPL.readsPerSec(), twoPL.writesPerSec(), twoPL.opsPerSec());
    std::printf("  deadlocks/aborts : %lld\n",
                static_cast<long long>(twoPL.aborts));
    std::printf("=== MVCC (snapshot isolation) ===\n");
    std::printf("  reads/s : %.0f   writes/s : %.0f   ops/s : %.0f\n",
                mvcc.readsPerSec(), mvcc.writesPerSec(), mvcc.opsPerSec());
    std::printf("  aborts (write conflicts) : %lld\n",
                static_cast<long long>(mvcc.aborts));

    const double readerSpeedup =
        twoPL.readsPerSec() > 0 ? mvcc.readsPerSec() / twoPL.readsPerSec() : 0.0;
    const double opsSpeedup =
        twoPL.opsPerSec()   > 0 ? mvcc.opsPerSec()   / twoPL.opsPerSec()   : 0.0;
    std::printf("\n=== MVCC vs 2PL ===\n");
    std::printf("  reader throughput speedup : %.2fx\n", readerSpeedup);
    std::printf("  total  throughput speedup : %.2fx\n", opsSpeedup);

    std::error_code ec;
    std::filesystem::create_directories("benchmark_results", ec);
    FILE* csv = std::fopen("benchmark_results/write.csv", "w");
    if (csv) {
        std::fprintf(csv,
            "protocol,n,readers,writers,seconds,reads,writes,aborts,reads_per_s,writes_per_s,ops_per_s\n");
        std::fprintf(csv, "2PL,%d,%d,%d,%.3f,%lld,%lld,%lld,%.0f,%.0f,%.0f\n",
            N, R, W, twoPL.seconds,
            static_cast<long long>(twoPL.reads), static_cast<long long>(twoPL.writes),
            static_cast<long long>(twoPL.aborts),
            twoPL.readsPerSec(), twoPL.writesPerSec(), twoPL.opsPerSec());
        std::fprintf(csv, "MVCC,%d,%d,%d,%.3f,%lld,%lld,%lld,%.0f,%.0f,%.0f\n",
            N, R, W, mvcc.seconds,
            static_cast<long long>(mvcc.reads), static_cast<long long>(mvcc.writes),
            static_cast<long long>(mvcc.aborts),
            mvcc.readsPerSec(), mvcc.writesPerSec(), mvcc.opsPerSec());
        std::fclose(csv);
    }
    return 0;
}