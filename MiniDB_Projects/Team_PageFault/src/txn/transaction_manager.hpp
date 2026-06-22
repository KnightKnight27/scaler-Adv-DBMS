#pragma once
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <set>
#include <mutex>
#include <condition_variable>
#include "transaction.hpp"
#include "../common/types.hpp"

namespace minidb {

// A thread-safe concurrency engine over an in-memory integer key/value store,
// supporting two interchangeable modes (this is the Track B extension):
//
//   MVCC   - each committed write appends a new version {value, commit_ts};
//            readers use their begin-time snapshot and take NO locks, so they
//            never block on writers.
//   TWO_PL - strict two-phase locking: reads take shared locks, writes take
//            exclusive locks, all released at commit/abort; readers and writers
//            block each other.
//
// Writes always take an exclusive lock (in both modes) so lost updates are
// impossible. Blocked lock requests are checked against a waits-for graph; a
// cycle aborts the requester (DeadlockAbort).
class TransactionManager {
public:
    enum class Mode { MVCC, TWO_PL };
    explicit TransactionManager(Mode m) : mode_(m) {}

    Mode mode() const { return mode_; }

    // Seed a committed value (before any concurrent transactions start).
    void setInitial(int key, int64_t value);

    Txn begin();
    int64_t read(Txn& t, int key);              // may throw DeadlockAbort (TWO_PL)
    void write(Txn& t, int key, int64_t value); // may throw DeadlockAbort
    void commit(Txn& t);
    void abort(Txn& t);

    size_t deadlockCount() const { return deadlocks_; }

private:
    enum class LockMode { SHARED, EXCLUSIVE };

    struct Version {
        int64_t commit_ts;
        int64_t value;
    };

    Mode mode_;
    std::mutex mtx_;
    std::condition_variable cv_;

    int64_t commit_clock_ = 0;
    TxnId next_id_ = 1;
    size_t deadlocks_ = 0;

    std::unordered_map<int, std::vector<Version>> history_;        // key -> versions (ts ascending)
    std::unordered_map<TxnId, std::unordered_map<int, int64_t>> writes_; // txn write buffers

    std::unordered_map<int, TxnId> xlock_;                         // key -> exclusive holder
    std::unordered_map<int, std::set<TxnId>> slock_;               // key -> shared holders
    std::unordered_map<TxnId, std::set<int>> held_x_, held_s_;     // per-txn held locks
    std::unordered_map<TxnId, std::set<TxnId>> waits_;             // waits-for graph

    // Must hold mtx_. Acquire a lock, blocking until granted; throws on deadlock.
    void acquire(std::unique_lock<std::mutex>& lk, Txn& t, int key, LockMode m);
    bool canGrant(TxnId txid, int key, LockMode m, std::set<TxnId>& blockers) const;
    bool hasCycle(TxnId start) const;
    void releaseAll(TxnId txid);
    int64_t visibleValue(const Txn& t, int key) const; // MVCC snapshot read
};

} // namespace minidb
