#include "TransactionManager.hpp"
#include <algorithm>

TransactionManager::TransactionManager() : global_xid_counter(1) {}

int TransactionManager::Begin() {
    int xid = global_xid_counter++;
    transactions[xid] = {xid, TxnState::ACTIVE, xid, {}};
    std::cout << "[BEGIN] Transaction " << xid << " started.\n";
    return xid;
}

bool TransactionManager::IsVisible(const Version& v, int txn_id) {
    const auto& txn = transactions[txn_id];

    bool xmin_valid = false;
    if (v.xmin == txn_id) {
        xmin_valid = true; 
    } else if (transactions.count(v.xmin) && transactions[v.xmin].state == TxnState::COMMITTED && v.xmin <= txn.snapshot_xid) {
        xmin_valid = true;
    }

    if (!xmin_valid) return false;

    if (v.xmax == 0) return true;
    if (v.xmax == txn_id) return false; // We deleted it
    
    if (transactions.count(v.xmax)) {
        if (transactions[v.xmax].state == TxnState::ABORTED) return true;
        if (transactions[v.xmax].state == TxnState::COMMITTED && v.xmax <= txn.snapshot_xid) return false;
    }

    return true;
}

bool TransactionManager::Read(int txn_id, int row_id, int& out_value) {
    if (transactions[txn_id].state != TxnState::ACTIVE) {
        std::cerr << "Error: Txn " << txn_id << " is not active.\n";
        return false;
    }

    if (table.find(row_id) != table.end()) {
        for (auto it = table[row_id].rbegin(); it != table[row_id].rend(); ++it) {
            if (IsVisible(*it, txn_id)) {
                out_value = it->data;
                std::cout << "[READ] Txn " << txn_id << " read row " << row_id << " = " << out_value << " (xmin=" << it->xmin << ")\n";
                return true;
            }
        }
    }
    
    std::cout << "[READ] Txn " << txn_id << " found no visible version for row " << row_id << ".\n";
    return false; 
}

bool TransactionManager::Write(int txn_id, int row_id, int value) {
    if (transactions[txn_id].state != TxnState::ACTIVE) return false;

    if (exclusive_locks.count(row_id) && exclusive_locks[row_id] != txn_id) {
        int blocking_txn = exclusive_locks[row_id];
        std::cout << "[WRITE BLOCK] Txn " << txn_id << " blocked by Txn " << blocking_txn << " on row " << row_id << ".\n";
        
        waits_for[txn_id].insert(blocking_txn);

        if (DetectDeadlock()) {
            std::cout << "[DEADLOCK] Cycle detected! Aborting youngest transaction: " << txn_id << "\n";
            AbortInternal(txn_id);
            return false;
        }
        return false;
    }

    exclusive_locks[row_id] = txn_id;
    transactions[txn_id].locks_held.insert(row_id);

    if (table.find(row_id) != table.end()) {
        for (auto& v : table[row_id]) {
            if (v.xmax == 0 && IsVisible(v, txn_id)) {
                v.xmax = txn_id; 
                break;
            }
        }
    }
    
    table[row_id].push_back({txn_id, 0, value});
    std::cout << "[WRITE] Txn " << txn_id << " wrote row " << row_id << " = " << value << " (Lock acquired).\n";
    return true;
}

void TransactionManager::Commit(int txn_id) {
    if (transactions[txn_id].state != TxnState::ACTIVE) return;

    transactions[txn_id].state = TxnState::COMMITTED;
    std::cout << "[COMMIT] Txn " << txn_id << " committed.\n";

    for (int row_id : transactions[txn_id].locks_held) {
        exclusive_locks.erase(row_id);
        std::cout << "         -> Released lock on row " << row_id << "\n";
    }
    transactions[txn_id].locks_held.clear();
    
    waits_for.erase(txn_id);
    for (auto& pair : waits_for) {
        pair.second.erase(txn_id);
    }
}

void TransactionManager::AbortInternal(int txn_id) {
    transactions[txn_id].state = TxnState::ABORTED;
    std::cout << "[ABORT] Txn " << txn_id << " aborted. Rolling back changes.\n";

    for (auto& pair : table) {
        auto& versions = pair.second;

        for (auto& v : versions) {
            if (v.xmax == txn_id) v.xmax = 0;
        }

        versions.erase(std::remove_if(versions.begin(), versions.end(), 
            [txn_id](const Version& v) { return v.xmin == txn_id; }), versions.end());
    }

    for (int row_id : transactions[txn_id].locks_held) {
        exclusive_locks.erase(row_id);
    }
    transactions[txn_id].locks_held.clear();

    waits_for.erase(txn_id);
    for (auto& pair : waits_for) {
        pair.second.erase(txn_id);
    }
}

void TransactionManager::Abort(int txn_id) {
    if (transactions[txn_id].state == TxnState::ACTIVE) {
        AbortInternal(txn_id);
    }
}

bool TransactionManager::DetectDeadlockHelper(int current, std::unordered_set<int>& visited, std::unordered_set<int>& recursion_stack) {
    visited.insert(current);
    recursion_stack.insert(current);

    for (int neighbor : waits_for[current]) {
        if (recursion_stack.count(neighbor)) {
            return true;
        }
        if (!visited.count(neighbor)) {
            if (DetectDeadlockHelper(neighbor, visited, recursion_stack)) return true;
        }
    }

    recursion_stack.erase(current);
    return false;
}

bool TransactionManager::DetectDeadlock() {
    std::unordered_set<int> visited;
    std::unordered_set<int> recursion_stack;

    for (const auto& pair : waits_for) {
        if (!visited.count(pair.first)) {
            if (DetectDeadlockHelper(pair.first, visited, recursion_stack)) {
                return true;
            }
        }
    }
    return false;
}