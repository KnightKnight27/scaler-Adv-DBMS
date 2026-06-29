#pragma once

#include <optional>
#include <ostream>
#include <utility>
#include <vector>

#include "Transaction.h"
#include "Types.h"

namespace txn {

// Abstractions the TransactionManager depends on. Each is a single, focused
// contract (Interface Segregation), and the manager is wired through these
// interfaces rather than the concrete classes (Dependency Inversion), so any
// piece can be swapped or stubbed in tests.

// A waits-for edge: `waiter` is blocked waiting for a lock held by `holder`.
using WaitsForEdge = std::pair<TxnId, TxnId>;

struct ILockManager {
    virtual ~ILockManager() = default;

    // Returns true if the lock is (now) granted to the transaction; false if
    // the request is incompatible and the transaction has been enqueued as a
    // waiter. Calling again after a blocker releases re-evaluates the request.
    virtual bool acquire(TxnId txn, const Key& key, LockMode mode) = 0;

    // Releases every lock and pending request held by the transaction. Under
    // Strict 2PL this is called only at commit or abort.
    virtual void release(TxnId txn) = 0;

    // Current waits-for edges, derived from granted vs. waiting requests.
    virtual std::vector<WaitsForEdge> waitsForEdges() const = 0;

    virtual void dump(std::ostream& out) const = 0;
};

struct IVersionStore {
    virtual ~IVersionStore() = default;

    // Snapshot read (MVCC): the transaction's own pending write if any,
    // otherwise the newest version committed at or before its start timestamp.
    // Used by lock-free read-only transactions.
    virtual std::optional<Value> readSnapshot(const Key& key, const Transaction& txn) const = 0;

    // Current read: the transaction's own pending write if any, otherwise the
    // newest committed version regardless of timestamp. Used by Strict-2PL
    // reads, where the held lock — not the snapshot — provides isolation.
    virtual std::optional<Value> readCurrent(const Key& key, const Transaction& txn) const = 0;

    // Appends a new uncommitted version created by `txn` (or overwrites the
    // transaction's own pending version for the key).
    virtual void put(const Key& key, Value value, TxnId txn) = 0;

    virtual void commit(TxnId txn, Timestamp commitTs) = 0;
    virtual void abort(TxnId txn) = 0;

    virtual void dump(std::ostream& out) const = 0;
};

struct IDeadlockDetector {
    virtual ~IDeadlockDetector() = default;

    // Returns a victim transaction to abort if the waits-for graph contains a
    // cycle, or std::nullopt if the graph is acyclic.
    virtual std::optional<TxnId> findVictim(const std::vector<WaitsForEdge>& edges) const = 0;
};

}  // namespace txn
