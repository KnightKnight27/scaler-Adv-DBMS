#pragma once

#include <memory>
#include <optional>
#include <ostream>
#include <unordered_map>

#include "Interfaces.h"
#include "Transaction.h"
#include "Types.h"

namespace txn {

// Orchestrates the three subsystems:
//   * Strict 2PL  - read/write acquire S/X locks held until commit or abort.
//   * MVCC        - writes append versions; reads resolve snapshot visibility.
//   * Deadlock    - a blocking lock request triggers cycle detection, and a
//                   victim is aborted automatically.
//
// The manager depends only on the interfaces; the default constructor wires
// the standard concrete components, while the injecting constructor accepts
// substitutes (e.g. for testing).
class TransactionManager {
public:
    explicit TransactionManager(std::ostream& log);
    TransactionManager(std::unique_ptr<ILockManager> locks,
                       std::unique_ptr<IVersionStore> versions,
                       std::unique_ptr<IDeadlockDetector> detector,
                       std::ostream& log);

    TxnId begin(bool readOnly = false);

    // Strict-2PL data operations for read-write transactions.
    ReadResult read(TxnId txn, const Key& key);
    OpStatus write(TxnId txn, const Key& key, Value value);

    // MVCC snapshot read for read-only transactions: never locks, never blocks.
    std::optional<Value> snapshotRead(TxnId txn, const Key& key) const;

    void commit(TxnId txn);
    void abort(TxnId txn);

    TxnState state(TxnId txn) const;

    void dumpVersions(std::ostream& out) const { versions_->dump(out); }
    void dumpLocks(std::ostream& out) const { locks_->dump(out); }

private:
    Transaction& require(TxnId txn);
    const Transaction& require(TxnId txn) const;

    // Acquires `mode` on `key` for `txn`, resolving deadlocks. Returns Ok if
    // granted, Blocked if the transaction must wait, or Aborted if `txn` was
    // selected as the deadlock victim.
    OpStatus acquireLock(Transaction& txn, const Key& key, LockMode mode);

    void rollback(Transaction& txn);  // shared abort path

    std::unique_ptr<ILockManager> locks_;
    std::unique_ptr<IVersionStore> versions_;
    std::unique_ptr<IDeadlockDetector> detector_;
    std::ostream& log_;

    std::unordered_map<TxnId, Transaction> txns_;
    TxnId nextTxnId_ = 1;
    Timestamp clock_ = 1;  // logical clock; bumped on every commit
};

}  // namespace txn
