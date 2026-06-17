// txn_manager.h — ADBMS Lab 8, 24BCS10115 Gauri Shukla
//
// An in-memory transaction manager combining:
//   * MVCC snapshot reads  — each transaction reads against the snapshot it
//     took at begin(); readers never block and never take locks.
//   * Strict two-phase locking for writes — a writer takes an exclusive lock
//     on each key it touches and holds every lock until commit/abort.
//   * Deadlock detection — a waits-for graph is checked on every blocked write;
//     a cycle aborts the youngest transaction in it.
//   * First-updater-wins — at commit a writer is rejected if a concurrent
//     transaction already committed a new version of a key it wrote.
//   * gc() — prunes dead versions no active or future snapshot can ever see.
//
// This is the PostgreSQL-style split: reads use MVCC visibility, writes use
// row locks. It is a single-threaded, deterministic simulator — a blocked
// write returns Status::LockWait and the caller retries once the holder
// releases, which keeps the deadlock scenarios reproducible.

#ifndef ADBMS_LAB8_TXN_MANAGER_H
#define ADBMS_LAB8_TXN_MANAGER_H

#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mvcc {

using TxId = unsigned long long;

enum class Status { Ok, NotFound, LockWait, Aborted, SerializationFailure };
const char* to_string(Status s);

enum class TxState { Active, Committed, Aborted };

class TxnManager {
public:
    TxnManager() = default;

    TxId begin();

    // MVCC read against the caller's snapshot (plus its own uncommitted writes).
    Status read(TxId tx, const std::string& key, std::string& out);

    // 2PL write: acquires the key's exclusive lock first.
    //   Ok        — buffered into the write-set,
    //   LockWait  — key locked by another live txn, no deadlock (retry later),
    //   Aborted   — this txn was chosen as the deadlock victim.
    Status write(TxId tx, const std::string& key, const std::string& value);
    Status remove(TxId tx, const std::string& key);

    // Ok, SerializationFailure (first-updater-wins), or Aborted (already dead).
    Status commit(TxId tx);
    void   abort(TxId tx);

    std::size_t gc();   // prune dead versions; returns how many were removed

    // --- introspection for the demo / tests -----------------------------
    TxState state(TxId tx) const;
    TxId    last_victim() const { return last_victim_; }
    std::size_t version_count() const;   // total versions across all keys

private:
    struct Version {
        std::string value;
        bool        deleted  = false;   // tombstone
        TxId        begin_ts = 0;       // commit timestamp of the creator
        TxId        end_ts   = 0;       // commit ts that superseded it (0 = live)
        TxId        creator  = 0;
    };

    struct Pending { std::string value; bool deleted; };

    struct Txn {
        TxId    id       = 0;
        TxId    snapshot = 0;
        TxState st       = TxState::Active;
        std::unordered_map<std::string, Pending> writes;
        std::unordered_set<std::string>          locks;
    };

    TxId next_id_ = 1;
    TxId clock_   = 0;          // global commit clock; commit_ts = ++clock_
    TxId last_victim_ = 0;

    std::unordered_map<TxId, Txn>                            txns_;
    std::unordered_map<std::string, std::vector<Version>>    store_;   // key -> chain
    std::unordered_map<std::string, TxId>                    xlock_;   // key -> holder
    std::unordered_map<TxId, TxId>                           waits_;   // waiter -> holder

    const Version* visible(const std::string& key, TxId snapshot) const;
    Status acquire_x(Txn& t, const std::string& key);
    void   release_locks(Txn& t);
    void   drop_wait_edges(TxId id);
    void   abort_internal(TxId id);
};

}  // namespace mvcc

#endif  // ADBMS_LAB8_TXN_MANAGER_H
