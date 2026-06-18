#pragma once
// transaction.h — Two-Phase Locking (core requirement) and MVCC (Track B).
//
// ── 2PL (LockManager) ──────────────────────────────────────────────────────
// Implements strict two-phase locking at the row level.
// Lock key = "tablename:pk_value" (e.g. "users:42").
//
// Growing phase: acquire shared (S) or exclusive (X) locks as needed.
// Shrinking phase: all locks released together at COMMIT or ABORT.
//
// Deadlock handling: a transaction that cannot acquire a lock within
// a timeout is aborted (victim selection by timeout simplicity).
// The exception Deadlock is thrown and the caller must roll back.
//
// ── MVCC (MvccStore) ─────────────────────────────────────────────────────────
// Used in the benchmark to contrast with 2PL.
// Each integer key carries a version chain (newest-first).
// A reader gets its snapshot timestamp at BEGIN and sees only versions
// committed before that timestamp — no locks taken, no waiting.
// Writers detect write-write conflicts (first-committer-wins) and abort
// the loser if two transactions modify the same key concurrently.
#include "value.h"
#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace minidb {

// ── Undo record — stored per transaction for ABORT rollback ──────────────────
struct UndoRecord {
    enum { INSERT, DELETE } op;
    std::string table;
    Value       key;
    std::string encoded_row;   // only used by DELETE undo (to re-insert)
};

// ── Transaction descriptor ────────────────────────────────────────────────────
struct Txn {
    long                    id;
    std::vector<std::string> locks;        // lock keys held under 2PL
    std::vector<UndoRecord>  undo;         // for ABORT rollback
    bool                    active = true;
};

// ── Deadlock exception ────────────────────────────────────────────────────────
struct DeadlockError : std::runtime_error {
    DeadlockError() : std::runtime_error("deadlock detected: transaction aborted") {}
};

// ── LockManager (strict 2PL) ──────────────────────────────────────────────────
class LockManager {
public:
    // Acquire a lock on `key` for transaction `t`.
    // exclusive=true  → X lock (only one holder allowed, no shared holders)
    // exclusive=false → S lock (multiple readers allowed)
    // Throws DeadlockError if the lock cannot be acquired within the timeout.
    void acquire(Txn* t, const std::string& key, bool exclusive);

    // Release all locks held by transaction `t` (called on COMMIT or ABORT).
    void release_all(Txn* t);

    std::atomic<long> deadlock_count{0};   // for benchmark reporting

private:
    struct LockEntry {
        std::unordered_set<long> shared_holders;  // transaction IDs with S locks
        long excl_holder = -1;                    // transaction ID with X lock; -1 = none
    };

    std::mutex              mu_;
    std::condition_variable cv_;
    std::unordered_map<std::string, LockEntry> table_;
};

// ── MVCC — used by the benchmark (Track B) ────────────────────────────────────

// One committed version of a key.
struct Version {
    long  commit_ts;  // timestamp when this version was committed
    bool  deleted;    // true if this version is a deletion marker
    Value val;
};

// An MVCC transaction: holds its snapshot timestamp and a private write buffer.
// The write buffer is not visible to other transactions until commit().
class MvccTxn {
public:
    explicit MvccTxn(long read_ts) : read_ts_(read_ts) {}
    long read_ts() const { return read_ts_; }

private:
    friend class MvccStore;
    long                           read_ts_;
    std::map<long, std::pair<bool, Value>> writes_;  // key → (deleted, value)
};

// Key-value store with snapshot isolation.
// Integer keys only (fine for the benchmark which uses account IDs).
class MvccStore {
public:
    MvccTxn begin_txn();                       // snapshot at current time
    bool    get(MvccTxn& t, long key, Value& out);  // reads as-of snapshot
    void    put(MvccTxn& t, long key, const Value& v);
    void    erase(MvccTxn& t, long key);
    bool    commit(MvccTxn& t);                // false = write-write conflict

    std::atomic<long> conflict_count{0};

private:
    std::atomic<long>   clock_{1};
    std::mutex          mu_;
    std::unordered_map<long, std::vector<Version>> chains_;  // key → versions (newest first)
};

} // namespace minidb
