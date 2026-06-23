// Transaction objects and the transaction manager.
//
// A Transaction tracks its id, state, and an in-memory undo list (closures that
// reverse each mutation, used for ROLLBACK and to keep the in-memory index
// consistent on abort). Crash recovery is handled separately by the WAL.
//
// The TransactionManager hands out monotonically increasing ids, owns the lock
// manager, and drives commit/abort (releasing locks; running undo on abort).
#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "common/types.hpp"
#include "transaction/lock_manager.hpp"

namespace minidb {

enum class TxnState { GROWING, COMMITTED, ABORTED };

class Transaction {
public:
    explicit Transaction(TxnId id) : id_(id) {}

    TxnId id() const { return id_; }
    TxnState state() const { return state_; }
    void set_state(TxnState s) { state_ = s; }

    // Push a reversal action; run in LIFO order on abort.
    void add_undo(std::function<void()> fn) { undo_.push_back(std::move(fn)); }
    std::vector<std::function<void()>>& undo() { return undo_; }

    bool auto_commit = false;   // single-statement implicit transaction

private:
    TxnId id_;
    TxnState state_ = TxnState::GROWING;
    std::vector<std::function<void()>> undo_;
};

class TransactionManager {
public:
    LockManager& locks() { return lock_mgr_; }

    Transaction* begin() {
        std::lock_guard<std::mutex> lk(mtx_);
        TxnId id = next_id_++;
        auto txn = std::make_unique<Transaction>(id);
        Transaction* ptr = txn.get();
        active_[id] = std::move(txn);
        return ptr;
    }

    void commit(Transaction* txn) {
        txn->set_state(TxnState::COMMITTED);
        lock_mgr_.release_all(txn->id());
        forget(txn->id());
    }

    // Run undo closures in reverse, release locks, drop the txn.
    void abort(Transaction* txn) {
        auto& u = txn->undo();
        for (auto it = u.rbegin(); it != u.rend(); ++it) (*it)();
        txn->set_state(TxnState::ABORTED);
        lock_mgr_.release_all(txn->id());
        forget(txn->id());
    }

    TxnId next_id() const { return next_id_; }

private:
    void forget(TxnId id) {
        std::lock_guard<std::mutex> lk(mtx_);
        active_.erase(id);
    }

    LockManager lock_mgr_;
    std::mutex mtx_;
    TxnId next_id_ = 1;
    std::unordered_map<TxnId, std::unique_ptr<Transaction>> active_;
};

}  // namespace minidb
