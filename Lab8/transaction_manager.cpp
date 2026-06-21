#include "transaction_manager.h"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <functional>

DeadlockException::DeadlockException(TxID xid)
    : std::runtime_error("Deadlock detected, aborting transaction " + std::to_string(xid)) {}

Transaction* TransactionManager::get_txn(TxID xid) {
    auto it = txns_.find(xid);
    if (it == txns_.end()) return nullptr;
    return &it->second;
}

const Transaction* TransactionManager::get_txn(TxID xid) const {
    auto it = txns_.find(xid);
    if (it == txns_.end()) return nullptr;
    return &it->second;
}

bool TransactionManager::is_visible(const RowVersion& v, const Transaction& reader) const {
    const Transaction* creator = get_txn(v.xmin);

    bool creator_visible =
        (v.xmin == reader.id) ||
        (creator != nullptr &&
         creator->status == TxStatus::COMMITTED &&
         v.xmin <= reader.snapshot);

    if (!creator_visible) return false;

    if (v.xmax == 0) return true;
    if (v.xmax == reader.id) return false;

    const Transaction* remover = get_txn(v.xmax);
    if (remover == nullptr) return true;
    if (remover->status == TxStatus::ABORTED) return true;

    bool removed_before_snapshot =
        remover->status == TxStatus::COMMITTED &&
        v.xmax <= reader.snapshot;

    return !removed_before_snapshot;
}

std::unordered_set<TxID> TransactionManager::blockers_for(
    const LockState& state,
    TxID xid,
    LockMode mode
) const {
    std::unordered_set<TxID> blockers;

    if (state.exclusive_holder != 0 && state.exclusive_holder != xid) {
        blockers.insert(state.exclusive_holder);
    }

    if (mode == LockMode::EXCLUSIVE) {
        for (TxID holder : state.shared_holders) {
            if (holder != xid) blockers.insert(holder);
        }
    }

    return blockers;
}

bool TransactionManager::has_cycle_from(TxID start) const {
    std::unordered_set<TxID> visited;
    std::unordered_set<TxID> in_stack;

    std::function<bool(TxID)> dfs = [&](TxID node) -> bool {
        if (in_stack.count(node)) return true;
        if (visited.count(node)) return false;

        visited.insert(node);
        in_stack.insert(node);

        auto it = waits_for_.find(node);
        if (it != waits_for_.end()) {
            for (TxID nxt : it->second) {
                if (dfs(nxt)) return true;
            }
        }

        in_stack.erase(node);
        return false;
    };

    return dfs(start);
}

void TransactionManager::acquire_lock(TxID xid, const RowKey& key, LockMode mode) {
    std::unique_lock<std::mutex> lk(mutex_);
    Transaction* tx = get_txn(xid);
    if (!tx || tx->status != TxStatus::ACTIVE) {
        throw std::runtime_error("Transaction is not active");
    }

    if (tx->shrinking) {
        throw std::runtime_error("2PL violation: transaction is in shrinking phase");
    }

    while (true) {
        LockState& state = locks_[key];

        bool already_ok = false;

        if (mode == LockMode::SHARED) {
            if (state.exclusive_holder == 0 || state.exclusive_holder == xid) {
                already_ok = true;
            }
        } else {
            bool no_other_shared = true;
            for (TxID holder : state.shared_holders) {
                if (holder != xid) {
                    no_other_shared = false;
                    break;
                }
            }

            bool no_other_exclusive =
                (state.exclusive_holder == 0 || state.exclusive_holder == xid);

            if (no_other_shared && no_other_exclusive) {
                already_ok = true;
            }
        }

        if (already_ok) {
            waits_for_.erase(xid);

            if (mode == LockMode::SHARED) {
                state.shared_holders.insert(xid);
            } else {
                state.shared_holders.erase(xid);
                state.exclusive_holder = xid;
            }

            tx->held_locks.insert(key);
            return;
        }

        waits_for_[xid] = blockers_for(state, xid, mode);

        if (has_cycle_from(xid)) {
            waits_for_.erase(xid);
            throw DeadlockException(xid);
        }

        cv_.wait(lk);
        tx = get_txn(xid);
        if (!tx || tx->status != TxStatus::ACTIVE) {
            throw std::runtime_error("Transaction is not active");
        }
    }
}

void TransactionManager::release_locks_unlocked(TxID xid) {
    Transaction* tx = get_txn(xid);
    if (!tx) return;

    tx->shrinking = true;

    for (const RowKey& key : tx->held_locks) {
        auto it = locks_.find(key);
        if (it == locks_.end()) continue;

        LockState& state = it->second;
        state.shared_holders.erase(xid);
        if (state.exclusive_holder == xid) {
            state.exclusive_holder = 0;
        }
    }

    tx->held_locks.clear();

    waits_for_.erase(xid);
    for (auto& [waiter, deps] : waits_for_) {
        deps.erase(xid);
    }

    cv_.notify_all();
}

void TransactionManager::rollback_unlocked(TxID xid) {
    for (auto& [key, chain] : rows_) {
        for (auto it = chain.begin(); it != chain.end(); ) {
            if (it->xmin == xid) {
                it = chain.erase(it);
            } else {
                if (it->xmax == xid) {
                    it->xmax = 0;
                }
                ++it;
            }
        }
    }
}

std::list<RowVersion>::iterator TransactionManager::find_visible_version(
    const RowKey& key,
    const Transaction& reader
) {
    auto it = rows_.find(key);
    if (it == rows_.end()) return std::list<RowVersion>::iterator{};

    auto& chain = it->second;
    for (auto rit = chain.begin(); rit != chain.end(); ++rit) {
        if (is_visible(*rit, reader)) {
            return rit;
        }
    }
    return chain.end();
}

TxID TransactionManager::begin() {
    std::lock_guard<std::mutex> lk(mutex_);
    TxID xid = next_id_++;
    txns_[xid] = Transaction{
        .id = xid,
        .snapshot = xid - 1,
        .status = TxStatus::ACTIVE,
        .shrinking = false
    };

    std::cout << "[TX " << xid << "] BEGIN\n";
    return xid;
}

std::optional<std::string> TransactionManager::read(TxID xid, const RowKey& key) {
    acquire_lock(xid, key, LockMode::SHARED);

    std::lock_guard<std::mutex> lk(mutex_);
    Transaction* tx = get_txn(xid);
    if (!tx || tx->status != TxStatus::ACTIVE) {
        return std::nullopt;
    }

    auto row_it = rows_.find(key);
    if (row_it == rows_.end()) return std::nullopt;

    for (const RowVersion& v : row_it->second) {
        if (is_visible(v, *tx)) {
            if (v.xmax == xid) return std::nullopt;
            return v.value;
        }
    }

    return std::nullopt;
}

void TransactionManager::insert(TxID xid, const RowKey& key, const std::string& value) {
    acquire_lock(xid, key, LockMode::EXCLUSIVE);

    std::lock_guard<std::mutex> lk(mutex_);
    Transaction* tx = get_txn(xid);
    if (!tx || tx->status != TxStatus::ACTIVE) {
        throw std::runtime_error("Transaction is not active");
    }

    rows_[key].push_front(RowVersion{value, xid, 0});
}

void TransactionManager::update(TxID xid, const RowKey& key, const std::string& value) {
    acquire_lock(xid, key, LockMode::EXCLUSIVE);

    std::lock_guard<std::mutex> lk(mutex_);
    Transaction* tx = get_txn(xid);
    if (!tx || tx->status != TxStatus::ACTIVE) {
        throw std::runtime_error("Transaction is not active");
    }

    auto row_it = rows_.find(key);
    if (row_it == rows_.end()) {
        throw std::runtime_error("Row not found: " + key);
    }

    bool updated = false;
    for (RowVersion& v : row_it->second) {
        if (v.xmax == 0 && is_visible(v, *tx)) {
            v.xmax = xid;
            updated = true;
            break;
        }
    }

    if (!updated) {
        throw std::runtime_error("No visible version found for: " + key);
    }

    row_it->second.push_front(RowVersion{value, xid, 0});
}

void TransactionManager::remove(TxID xid, const RowKey& key) {
    acquire_lock(xid, key, LockMode::EXCLUSIVE);

    std::lock_guard<std::mutex> lk(mutex_);
    Transaction* tx = get_txn(xid);
    if (!tx || tx->status != TxStatus::ACTIVE) {
        throw std::runtime_error("Transaction is not active");
    }

    auto row_it = rows_.find(key);
    if (row_it == rows_.end()) return;

    for (RowVersion& v : row_it->second) {
        if (v.xmax == 0 && is_visible(v, *tx)) {
            v.xmax = xid;
            return;
        }
    }
}

void TransactionManager::commit(TxID xid) {
    std::lock_guard<std::mutex> lk(mutex_);
    Transaction* tx = get_txn(xid);
    if (!tx || tx->status != TxStatus::ACTIVE) {
        throw std::runtime_error("Transaction is not active");
    }

    tx->status = TxStatus::COMMITTED;
    release_locks_unlocked(xid);
    std::cout << "[TX " << xid << "] COMMIT\n";
}

void TransactionManager::abort(TxID xid) {
    std::lock_guard<std::mutex> lk(mutex_);
    Transaction* tx = get_txn(xid);
    if (!tx || tx->status != TxStatus::ACTIVE) {
        return;
    }

    rollback_unlocked(xid);
    tx->status = TxStatus::ABORTED;
    release_locks_unlocked(xid);
    std::cout << "[TX " << xid << "] ABORT\n";
}