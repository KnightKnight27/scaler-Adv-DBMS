// Lab 6 - Transaction Manager: MVCC + Two-Phase Locking (header)
//
// A small transaction manager that combines three ideas:
//
//   1. MVCC  - every write creates a new row version stamped with the
//      writer's transaction id (xmin) and, when superseded, the
//      deleter's id (xmax). A reader sees the version that was committed
//      before its snapshot, so readers never block writers.
//
//   2. Strict 2PL - a transaction acquires shared/exclusive locks while
//      running (growing phase) and releases them all at commit/abort
//      (shrinking phase). It may not acquire a lock once it has started
//      releasing.
//
//   3. Deadlock detection - a waits-for graph; if acquiring a lock would
//      create a cycle, the requester is aborted with DeadlockException.
//
// This mirrors the core of PostgreSQL's concurrency model.

#ifndef LAB6_TRANSACTION_MANAGER_H_
#define LAB6_TRANSACTION_MANAGER_H_

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

namespace lab6 {

using TxId   = std::uint64_t;
using RowKey = std::string;

enum class LockMode { kShared, kExclusive };

// Thrown by read/insert/update/remove when granting the lock would
// deadlock. The caller is expected to abort the transaction.
class DeadlockException : public std::runtime_error {
public:
    explicit DeadlockException(TxId xid)
        : std::runtime_error("deadlock detected, abort tx " + std::to_string(xid)),
          tx(xid) {}
    TxId tx;
};

// The public API. All state lives inside the .cc file (a single global
// manager) to keep the demo simple; methods are thread-safe.
class TransactionManager {
public:
    TxId begin();

    // Each of these takes the appropriate lock (shared for read,
    // exclusive for writes) and may throw DeadlockException.
    std::optional<std::string> read(TxId xid, const RowKey& key);
    void insert(TxId xid, const RowKey& key, const std::string& value);
    void update(TxId xid, const RowKey& key, const std::string& value);
    void remove(TxId xid, const RowKey& key);

    void commit(TxId xid);
    void abort(TxId xid);
};

}  // namespace lab6

#endif  // LAB6_TRANSACTION_MANAGER_H_
