#include "transaction_manager.h"

#include <functional>
#include <iostream>

DeadlockException::DeadlockException(TxID xid)
    : std::runtime_error("Deadlock detected; aborting transaction " + std::to_string(xid)) {}

void TransactionManager::requireActive(TxID xid) const {
    auto found = transactions.find(xid);
    if(found == transactions.end() || found->second.status != TxStatus::ACTIVE) {
        throw std::runtime_error("Transaction is not active");
    }
}

bool TransactionManager::isVisible(const RowVersion& version, const Transaction& reader) const {
    auto creator = transactions.find(version.xmin);

    bool creatorVisible = version.xmin == reader.id ||
        (creator != transactions.end() &&
         creator->second.status == TxStatus::COMMITTED &&
         version.xmin <= reader.snapshotId);

    if(!creatorVisible) return false;
    if(version.xmax == 0) return true;
    if(version.xmax == reader.id) return false;

    auto remover = transactions.find(version.xmax);
    if(remover == transactions.end()) return true;
    if(remover->second.status == TxStatus::ABORTED) return true;

    bool removedBeforeSnapshot =
        remover->second.status == TxStatus::COMMITTED &&
        version.xmax <= reader.snapshotId;

    return !removedBeforeSnapshot;
}

std::unordered_set<TxID> TransactionManager::blockers(
    const LockState& state, TxID xid, LockMode mode) const {
    std::unordered_set<TxID> result;

    if(state.exclusiveHolder != 0 && state.exclusiveHolder != xid) {
        result.insert(state.exclusiveHolder);
    }

    if(mode == LockMode::EXCLUSIVE) {
        for(TxID holder : state.sharedHolders) {
            if(holder != xid) result.insert(holder);
        }
    }

    return result;
}

bool TransactionManager::hasCycle(TxID start) const {
    std::unordered_set<TxID> visited;
    std::unordered_set<TxID> activePath;

    std::function<bool(TxID)> visit = [&](TxID xid) {
        if(activePath.count(xid)) return true;
        if(visited.count(xid)) return false;

        visited.insert(xid);
        activePath.insert(xid);

        auto edges = waitsFor.find(xid);
        if(edges != waitsFor.end()) {
            for(TxID next : edges->second) {
                if(visit(next)) return true;
            }
        }

        activePath.erase(xid);
        return false;
    };

    return visit(start);
}

void TransactionManager::acquireLock(TxID xid, const RowKey& key, LockMode mode) {
    std::unique_lock<std::mutex> guard(mutex);
    requireActive(xid);

    if(transactions.at(xid).shrinking) {
        throw std::runtime_error("2PL violation: transaction is shrinking");
    }

    {
        LockState& state = locks[key];

        if(mode == LockMode::SHARED &&
           (state.sharedHolders.count(xid) || state.exclusiveHolder == xid)) {
            return;
        }

        if(mode == LockMode::EXCLUSIVE && state.exclusiveHolder == xid) return;
    }

    while(true) {
        {
            LockState& state = locks[key];
            std::unordered_set<TxID> blockedBy = blockers(state, xid, mode);

            if(blockedBy.empty()) {
                waitsFor.erase(xid);

                if(mode == LockMode::SHARED) {
                    state.sharedHolders.insert(xid);
                }
                else {
                    state.sharedHolders.erase(xid);
                    state.exclusiveHolder = xid;
                }
                return;
            }

            waitsFor[xid] = blockedBy;
            if(hasCycle(xid)) {
                waitsFor.erase(xid);
                throw DeadlockException(xid);
            }
        }

        lockChanged.wait(guard);
        requireActive(xid);
    }
}

void TransactionManager::releaseLocks(TxID xid) {
    transactions.at(xid).shrinking = true;

    for(auto& entry : locks) {
        LockState& state = entry.second;
        state.sharedHolders.erase(xid);
        if(state.exclusiveHolder == xid) state.exclusiveHolder = 0;
    }

    waitsFor.erase(xid);
    for(auto& entry : waitsFor) entry.second.erase(xid);

    lockChanged.notify_all();
}

TxID TransactionManager::begin() {
    std::lock_guard<std::mutex> guard(mutex);

    TxID xid = nextId++;
    transactions[xid] = {xid, xid - 1, TxStatus::ACTIVE, false};

    std::cout << "[TX " << xid << "] BEGIN\n";
    return xid;
}

std::optional<std::string> TransactionManager::read(TxID xid, const RowKey& key) {
    acquireLock(xid, key, LockMode::SHARED);
    std::lock_guard<std::mutex> guard(mutex);
    requireActive(xid);

    auto row = rows.find(key);
    if(row == rows.end()) return std::nullopt;

    const Transaction& reader = transactions.at(xid);
    for(const RowVersion& version : row->second) {
        if(isVisible(version, reader)) return version.value;
    }

    return std::nullopt;
}

void TransactionManager::insert(TxID xid, const RowKey& key, const std::string& value) {
    acquireLock(xid, key, LockMode::EXCLUSIVE);
    std::lock_guard<std::mutex> guard(mutex);
    requireActive(xid);
    rows[key].push_front({value, xid, 0});
}

void TransactionManager::update(TxID xid, const RowKey& key, const std::string& value) {
    acquireLock(xid, key, LockMode::EXCLUSIVE);
    std::lock_guard<std::mutex> guard(mutex);
    requireActive(xid);

    auto row = rows.find(key);
    if(row == rows.end()) throw std::runtime_error("Row not found: " + key);

    Transaction& writer = transactions.at(xid);
    bool updated = false;

    for(RowVersion& version : row->second) {
        if(version.xmax == 0 && isVisible(version, writer)) {
            version.xmax = xid;
            updated = true;
            break;
        }
    }

    if(!updated) throw std::runtime_error("No visible version for: " + key);
    row->second.push_front({value, xid, 0});
}

void TransactionManager::remove(TxID xid, const RowKey& key) {
    acquireLock(xid, key, LockMode::EXCLUSIVE);
    std::lock_guard<std::mutex> guard(mutex);
    requireActive(xid);

    auto row = rows.find(key);
    if(row == rows.end()) return;

    Transaction& writer = transactions.at(xid);
    for(RowVersion& version : row->second) {
        if(version.xmax == 0 && isVisible(version, writer)) {
            version.xmax = xid;
            return;
        }
    }
}

void TransactionManager::commit(TxID xid) {
    std::lock_guard<std::mutex> guard(mutex);
    requireActive(xid);

    transactions.at(xid).status = TxStatus::COMMITTED;
    releaseLocks(xid);
    std::cout << "[TX " << xid << "] COMMIT\n";
}

void TransactionManager::abort(TxID xid) {
    std::lock_guard<std::mutex> guard(mutex);

    auto transaction = transactions.find(xid);
    if(transaction == transactions.end() ||
       transaction->second.status != TxStatus::ACTIVE) {
        return;
    }

    for(auto& row : rows) {
        for(RowVersion& version : row.second) {
            if(version.xmax == xid) version.xmax = 0;
        }
    }

    transaction->second.status = TxStatus::ABORTED;
    releaseLocks(xid);
    std::cout << "[TX " << xid << "] ABORT\n";
}
