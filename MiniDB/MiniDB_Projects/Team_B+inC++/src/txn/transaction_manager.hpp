#pragma once

#include <condition_variable>
#include <list>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "transaction.hpp"

// Two concurrency-control modes, so the benchmark can run the SAME workload both
// ways and compare. The ONLY behavioural difference is in read():
//   MVCC    — reads take no lock and see a consistent snapshot (never block);
//   TWO_PL  — reads take a shared lock (so they block, and are blocked by, writers).
// Writes take an exclusive lock in both modes (that's the "2PL for writes" the
// core requirement needs); MVCC just lets readers bypass it.
enum class ConcurrencyMode { MVCC, TWO_PL };

using RowKey = std::string;

class DeadlockException : public std::runtime_error {
public:
    explicit DeadlockException(TxID xid);
};

// A thread-safe MVCC + Strict-2PL transaction engine over an in-memory,
// versioned key→value store. Rows loaded from disk are seeded with load_committed
// (genesis); committed writes are read back out for write-through to the heap.
class TransactionManager {
public:
    explicit TransactionManager(ConcurrencyMode mode = ConcurrencyMode::MVCC);

    TxID begin();
    std::optional<std::string> read(TxID xid, const RowKey& key);
    void write(TxID xid, const RowKey& key, const std::string& value);  // insert or update
    void erase(TxID xid, const RowKey& key);
    void commit(TxID xid);
    void abort(TxID xid);

    // Seed a committed (genesis) row — used when loading a table from the heap.
    void load_committed(const RowKey& key, const std::string& value);
    // All (key,value) pairs visible to `xid`'s snapshot — used by transactional SELECT.
    std::vector<std::pair<RowKey, std::string>> snapshot_scan(TxID xid);

    ConcurrencyMode mode() const { return mode_; }

private:
    enum class LockMode { SHARED, EXCLUSIVE };

    // One version of a row. xmax == 0 means "not deleted". A delete or update
    // stamps the old version's xmax with the writer's xid.
    struct RowVersion { std::string value; TxID xmin; TxID xmax; };
    struct LockRequest { TxID xid; LockMode mode; bool granted; };

    ConcurrencyMode mode_;

    // Transaction table.
    std::mutex                              tx_mu_;
    TxID                                    next_xid_ = 1;
    std::unordered_map<TxID, Transaction>   txns_;

    // Versioned store.
    std::mutex                                            heap_mu_;
    std::unordered_map<RowKey, std::list<RowVersion>>     heap_;  // newest version first

    // Lock manager: one mutex + one CV guard the whole lock table and waits-for
    // graph (simple and deadlock-free; per-key parallelism isn't the goal — the
    // logical blocking behaviour we measure is).
    std::mutex                                                lm_mu_;
    std::condition_variable                                   lm_cv_;
    std::unordered_map<RowKey, std::list<LockRequest>>        lock_table_;
    std::unordered_map<TxID, std::unordered_set<TxID>>        waits_for_;

    bool is_committed(TxID xid);
    bool is_visible(const RowVersion& v, TxID snapshot, TxID reader);
    void acquire_lock(TxID xid, const RowKey& key, LockMode mode);
    void release_locks(TxID xid);
    bool has_cycle(TxID start);  // called with lm_mu_ held
};
