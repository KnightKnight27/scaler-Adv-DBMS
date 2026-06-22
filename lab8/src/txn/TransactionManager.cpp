#include "txn/TransactionManager.h"

#include <stdexcept>

#include "txn/DeadlockDetector.h"
#include "txn/LockManager.h"
#include "txn/VersionStore.h"

namespace txn {

TransactionManager::TransactionManager(std::ostream& log)
    : locks_(std::make_unique<LockManager>()),
      versions_(std::make_unique<VersionStore>()),
      detector_(std::make_unique<DeadlockDetector>()),
      log_(log) {}

TransactionManager::TransactionManager(std::unique_ptr<ILockManager> locks,
                                       std::unique_ptr<IVersionStore> versions,
                                       std::unique_ptr<IDeadlockDetector> detector,
                                       std::ostream& log)
    : locks_(std::move(locks)),
      versions_(std::move(versions)),
      detector_(std::move(detector)),
      log_(log) {}

Transaction& TransactionManager::require(TxnId txn) {
    auto it = txns_.find(txn);
    if (it == txns_.end()) throw std::runtime_error("unknown transaction T" + std::to_string(txn));
    return it->second;
}

const Transaction& TransactionManager::require(TxnId txn) const {
    auto it = txns_.find(txn);
    if (it == txns_.end()) throw std::runtime_error("unknown transaction T" + std::to_string(txn));
    return it->second;
}

TxnId TransactionManager::begin(bool readOnly) {
    TxnId id = nextTxnId_++;
    Transaction t;
    t.id = id;
    t.startTs = clock_;  // snapshot of all versions committed so far
    t.state = TxnState::Active;
    t.readOnly = readOnly;
    txns_[id] = t;
    log_ << "T" << id << " begin" << (readOnly ? " (read-only, snapshot ts=" : " (ts=") << t.startTs
         << ")\n";
    return id;
}

OpStatus TransactionManager::acquireLock(Transaction& txn, const Key& key, LockMode mode) {
    if (locks_->acquire(txn.id, key, mode)) {
        return OpStatus::Ok;
    }

    // The request is incompatible and now queued. Check whether queueing it
    // closed a cycle in the waits-for graph.
    log_ << "  T" << txn.id << " waits for " << toString(mode) << "-lock on " << key << '\n';
    auto victim = detector_->findVictim(locks_->waitsForEdges());
    if (!victim) {
        return OpStatus::Blocked;
    }

    log_ << "  ! deadlock detected -> aborting victim T" << *victim << '\n';
    rollback(require(*victim));

    if (*victim == txn.id) {
        return OpStatus::Aborted;
    }
    // A different transaction was rolled back; its locks are gone, so retry.
    return locks_->acquire(txn.id, key, mode) ? OpStatus::Ok : OpStatus::Blocked;
}

ReadResult TransactionManager::read(TxnId txn, const Key& key) {
    Transaction& t = require(txn);
    if (t.state != TxnState::Active) {
        return {OpStatus::Aborted, std::nullopt};
    }
    if (t.readOnly) {
        return {OpStatus::Ok, versions_->readSnapshot(key, t)};
    }

    OpStatus status = acquireLock(t, key, LockMode::Shared);
    if (status != OpStatus::Ok) {
        return {status, std::nullopt};
    }
    return {OpStatus::Ok, versions_->readCurrent(key, t)};
}

OpStatus TransactionManager::write(TxnId txn, const Key& key, Value value) {
    Transaction& t = require(txn);
    if (t.state != TxnState::Active) {
        return OpStatus::Aborted;
    }
    if (t.readOnly) {
        throw std::runtime_error("read-only transaction T" + std::to_string(txn) + " cannot write");
    }

    OpStatus status = acquireLock(t, key, LockMode::Exclusive);
    if (status != OpStatus::Ok) {
        return status;
    }
    versions_->put(key, value, t.id);
    return OpStatus::Ok;
}

std::optional<Value> TransactionManager::snapshotRead(TxnId txn, const Key& key) const {
    const Transaction& t = require(txn);
    return versions_->readSnapshot(key, t);
}

void TransactionManager::commit(TxnId txn) {
    Transaction& t = require(txn);
    if (t.state != TxnState::Active) return;

    if (!t.readOnly) {
        t.commitTs = ++clock_;  // a fresh tick, strictly greater than any snapshot taken so far
        versions_->commit(t.id, t.commitTs);
        locks_->release(t.id);
        log_ << "T" << t.id << " commit (ts=" << t.commitTs << ")\n";
    } else {
        log_ << "T" << t.id << " commit (read-only)\n";
    }
    t.state = TxnState::Committed;
}

void TransactionManager::rollback(Transaction& txn) {
    versions_->abort(txn.id);
    locks_->release(txn.id);
    txn.state = TxnState::Aborted;
}

void TransactionManager::abort(TxnId txn) {
    Transaction& t = require(txn);
    if (t.state != TxnState::Active) return;
    rollback(t);
    log_ << "T" << t.id << " abort\n";
}

TxnState TransactionManager::state(TxnId txn) const { return require(txn).state; }

}  // namespace txn
