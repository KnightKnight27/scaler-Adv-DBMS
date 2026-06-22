#pragma once

#include "lock_manager.h"
#include <functional>
#include <atomic>
#include <vector>

namespace minidb {

// transactionmanager — coordinates transactions, 2pl, and recovery
// manages transaction lifecycles: begin → active → committed/aborted.
// enforces two-phase locking: all locks acquired during growing phase,
// all released during shrinking phase (at commit/abort).

enum class TxnState { IDLE, ACTIVE, COMMITTING, COMMITTED, ABORTING, ABORTED };

struct Transaction {
    TxnID   id;
    TxnState state = TxnState::IDLE;
    // list of operations performed (for wal logging and undo)
    struct Operation {
        TableID     table_id;
        Key         key;
        Record      before_image;  // for undo
        Record      after_image;   // for redo
    };
    std::vector<Operation> operations;

    // timestamp (used for deadlock victim selection)
    std::chrono::steady_clock::time_point start_time;
};

class TransactionManager {
public:
    TransactionManager(LockManager& lock_mgr);

    // begin a new transaction.  returns the new transaction id.
    TxnID begin();

    // commit a transaction: flush wal, release locks, mark committed.
    // calls `wal_callback` with each log record if set.
    bool commit(TxnID txn_id);

    // abort (rollback) a transaction: undo changes, release locks.
    bool abort(TxnID txn_id);

    // record a modification within an active transaction.
    // used for wal logging so we can undo/redo later.
    void log_operation(TxnID txn_id, TableID table_id,
                       const Key& key,
                       const Record& before,
                       const Record& after);

    // retrieve transaction state.
    TxnState get_state(TxnID txn_id) const;

    // check if a transaction is active.
    bool is_active(TxnID txn_id) const;

    // for the recoveyr manager: iterate all active (uncommitted) transactions.
    const std::unordered_map<TxnID, Transaction>& transactions() const {
        return _transactions;
    }

    // set wal callback (called when logging operations).
    using WalCallback = std::function<void(TxnID, TableID, const Key&,
                                            const Record&, const Record&)>;
    void set_wal_callback(WalCallback cb) { _wal_callback = std::move(cb); }

    // mark a transaction as committed (used during recovery replay).
    void mark_committed(TxnID txn_id);

    LockManager& lock_manager() { return _lock_mgr; }

private:
    LockManager&                     _lock_mgr;
    std::atomic<TxnID>               _next_txn_id{1};
    std::unordered_map<TxnID, Transaction> _transactions;
    mutable std::mutex               _mutex;
    WalCallback                      _wal_callback;
};

// implementation

inline TransactionManager::TransactionManager(LockManager& lock_mgr)
    : _lock_mgr(lock_mgr) {}

inline TxnID TransactionManager::begin() {
    std::lock_guard<std::mutex> lock(_mutex);
    TxnID id = _next_txn_id++;
    Transaction txn;
    txn.id = id;
    txn.state = TxnState::ACTIVE;
    txn.start_time = std::chrono::steady_clock::now();
    _transactions[id] = txn;
    return id;
}

inline bool TransactionManager::commit(TxnID txn_id) {
    std::lock_guard<std::mutex> lock(_mutex);

    auto it = _transactions.find(txn_id);
    if (it == _transactions.end() || it->second.state != TxnState::ACTIVE) {
        return false;
    }

    it->second.state = TxnState::COMMITTING;

    // write wal commit record
    if (_wal_callback) {
        for (const auto& op : it->second.operations) {
            _wal_callback(txn_id, op.table_id, op.key,
                          op.before_image, op.after_image);
        }
    }

    // release all locks (shrinking phase)
    _lock_mgr.release_all(txn_id);

    it->second.state = TxnState::COMMITTED;
    return true;
}

inline bool TransactionManager::abort(TxnID txn_id) {
    std::lock_guard<std::mutex> lock(_mutex);

    auto it = _transactions.find(txn_id);
    if (it == _transactions.end() || it->second.state != TxnState::ACTIVE) {
        return false;
    }

    it->second.state = TxnState::ABORTING;

    // release all locks
    _lock_mgr.release_all(txn_id);

    it->second.state = TxnState::ABORTED;
    return true;
}

inline void TransactionManager::log_operation(TxnID txn_id, TableID table_id,
                                               const Key& key,
                                               const Record& before,
                                               const Record& after) {
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _transactions.find(txn_id);
    if (it != _transactions.end() && it->second.state == TxnState::ACTIVE) {
        it->second.operations.push_back({table_id, key, before, after});
    }
}

inline TxnState TransactionManager::get_state(TxnID txn_id) const {
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _transactions.find(txn_id);
    return (it != _transactions.end()) ? it->second.state : TxnState::IDLE;
}

inline bool TransactionManager::is_active(TxnID txn_id) const {
    return get_state(txn_id) == TxnState::ACTIVE;
}

inline void TransactionManager::mark_committed(TxnID txn_id) {
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _transactions.find(txn_id);
    if (it != _transactions.end()) {
        it->second.state = TxnState::COMMITTED;
    }
}

} // namespace minidb
