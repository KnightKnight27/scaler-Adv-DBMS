#include "transaction_manager.h"
#include <algorithm>
#include <iostream>

// ==============================
// LockManager Implementation
// ==============================
bool LockManager::DetectCycle(TxnId start_txn) {
    TxnId current = start_txn;
    std::unordered_set<TxnId> visited;
    
    while (waits_for.count(current)) {
        TxnId next = waits_for[current];
        if (next == start_txn) return true; 
        if (visited.count(next)) break;
        visited.insert(next);
        current = next;
    }
    return false;
}

bool LockManager::AcquireLock(TxnId txn_id, RowId row_id) {
    std::unique_lock<std::mutex> lock(mtx);
    while (exclusive_locks.count(row_id) && exclusive_locks[row_id] != txn_id) {
        TxnId blocking_txn = exclusive_locks[row_id];
        waits_for[txn_id] = blocking_txn;
        
        if (DetectCycle(txn_id)) {
            waits_for.erase(txn_id);
            return false; // Deadlock
        }
        
        cvs[row_id].wait(lock);
        waits_for.erase(txn_id);
    }
    exclusive_locks[row_id] = txn_id;
    return true;
}

void LockManager::ReleaseLocks(TxnId txn_id, const std::vector<RowId>& locked_rows) {
    std::lock_guard<std::mutex> lock(mtx);
    for (RowId row_id : locked_rows) {
        if (exclusive_locks[row_id] == txn_id) {
            exclusive_locks.erase(row_id);
            cvs[row_id].notify_all();
        }
    }
    waits_for.erase(txn_id);
}


// ==============================
// Database Implementation
// ==============================
bool Database::IsVisible(const Transaction& txn, const Version& v, const std::unordered_set<TxnId>& committed_txns) {
    bool is_xmin_visible = false;
    if (v.xmin == txn.id) {
        is_xmin_visible = true;
    } else if (v.xmin > txn.id) {
        is_xmin_visible = false;
    } else {
        bool xmin_committed = committed_txns.count(v.xmin) > 0;
        bool xmin_active_in_snapshot = txn.active_snapshot.count(v.xmin) > 0;
        is_xmin_visible = xmin_committed && !xmin_active_in_snapshot;
    }

    if (!is_xmin_visible) return false;

    bool is_xmax_visible = false;
    if (v.xmax == 0) {
        is_xmax_visible = false;
    } else if (v.xmax == txn.id) {
        is_xmax_visible = true;
    } else if (v.xmax > txn.id) {
        is_xmax_visible = false;
    } else {
        bool xmax_committed = committed_txns.count(v.xmax) > 0;
        bool xmax_active_in_snapshot = txn.active_snapshot.count(v.xmax) > 0;
        is_xmax_visible = xmax_committed && !xmax_active_in_snapshot;
    }

    return !is_xmax_visible;
}

void Database::InsertInitialData(RowId row_id, const std::string& data, TxnId init_txn_id) {
    std::lock_guard<std::mutex> lock(mtx);
    Version v;
    v.xmin = init_txn_id;
    v.xmax = 0;
    v.data = data;
    rows[row_id].push_back(v);
}

std::optional<std::string> Database::Read(Transaction& txn, RowId row_id, const std::unordered_set<TxnId>& committed_txns) {
    std::lock_guard<std::mutex> lock(mtx);
    if (rows.find(row_id) == rows.end()) return std::nullopt;

    auto& versions = rows[row_id];
    for (auto it = versions.rbegin(); it != versions.rend(); ++it) {
        if (IsVisible(txn, *it, committed_txns)) return it->data;
    }
    return std::nullopt;
}

bool Database::Update(Transaction& txn, RowId row_id, const std::string& data) {
    if (!lock_manager.AcquireLock(txn.id, row_id)) return false; 

    if (std::find(txn.locked_rows.begin(), txn.locked_rows.end(), row_id) == txn.locked_rows.end()) {
        txn.locked_rows.push_back(row_id);
    }

    std::lock_guard<std::mutex> lock(mtx);
    if (rows.find(row_id) != rows.end()) {
        auto& versions = rows[row_id];
        for (auto it = versions.rbegin(); it != versions.rend(); ++it) {
            if (it->xmax == 0 || it->xmax == txn.id) {
                if (it->xmin == txn.id) {
                    it->data = data;
                    return true;
                }
                it->xmax = txn.id;
                break;
            }
        }
    }
    
    Version v;
    v.xmin = txn.id;
    v.xmax = 0;
    v.data = data;
    rows[row_id].push_back(v);
    return true;
}

void Database::RollbackWrites(TxnId txn_id, const std::vector<RowId>& locked_rows) {
    std::lock_guard<std::mutex> lock(mtx);
    for (RowId row_id : locked_rows) {
        if (rows.find(row_id) != rows.end()) {
            auto& versions = rows[row_id];
            versions.erase(std::remove_if(versions.begin(), versions.end(),
                               [txn_id](const Version& v) { return v.xmin == txn_id; }),
                           versions.end());
            for (auto& v : versions) {
                if (v.xmax == txn_id) v.xmax = 0;
            }
        }
    }
}


// ==============================
// TransactionManager Implementation
// ==============================
std::unique_ptr<Transaction> TransactionManager::Begin() {
    std::lock_guard<std::mutex> lock(mtx);
    TxnId id = next_txn_id++;
    active_txns.insert(id);
    
    std::unordered_set<TxnId> snapshot = active_txns;
    snapshot.erase(id);
    
    return std::make_unique<Transaction>(id, snapshot);
}

void TransactionManager::Commit(Transaction& txn) {
    if (txn.state != TxnState::ACTIVE) return;
    
    lock_manager.ReleaseLocks(txn.id, txn.locked_rows);
    txn.locked_rows.clear();

    std::lock_guard<std::mutex> lock(mtx);
    active_txns.erase(txn.id);
    committed_txns.insert(txn.id);
    txn.state = TxnState::COMMITTED;
}

void TransactionManager::Abort(Transaction& txn) {
    if (txn.state != TxnState::ACTIVE) return;
    
    db.RollbackWrites(txn.id, txn.locked_rows);
    lock_manager.ReleaseLocks(txn.id, txn.locked_rows);
    txn.locked_rows.clear();

    std::lock_guard<std::mutex> lock(mtx);
    active_txns.erase(txn.id);
    txn.state = TxnState::ABORTED;
}
