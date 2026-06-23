#include "mvcc_db.h"
#include <iostream>
#include <algorithm>
#include <stdexcept>
#include <thread>

// =========================================================================
// LockManager Implementation
// =========================================================================

bool LockManager::detectCycleDFS(int node, std::unordered_map<int, bool>& visited, 
                                 std::unordered_map<int, bool>& recStack, std::vector<int>& path) {
    if (!visited[node]) {
        visited[node] = true;
        recStack[node] = true;
        path.push_back(node);

        auto it = wait_for_graph.find(node);
        if (it != wait_for_graph.end()) {
            for (int neighbor : it->second) {
                if (!visited[neighbor] && detectCycleDFS(neighbor, visited, recStack, path)) {
                    return true;
                } else if (recStack[neighbor]) {
                    path.push_back(neighbor);
                    return true;
                }
            }
        }
    }
    recStack[node] = false;
    if (!path.empty()) path.pop_back();
    return false;
}

void LockManager::buildGraph() {
    wait_for_graph.clear();
    // For each waiting transaction, add an edge to the transaction currently holding the lock
    for (const auto& pair : tx_waiting_on_key) {
        int waiting_tx = pair.first;
        int key = pair.second;
        
        auto it = lock_table.find(key);
        if (it != lock_table.end() && it->second != 0) {
            int holding_tx = it->second;
            wait_for_graph[waiting_tx].insert(holding_tx);
        }
    }
}

int LockManager::detectDeadlock() {
    buildGraph();
    std::unordered_map<int, bool> visited;
    std::unordered_map<int, bool> recStack;
    
    for (const auto& pair : wait_for_graph) {
        int start_node = pair.first;
        if (!visited[start_node]) {
            std::vector<int> path;
            if (detectCycleDFS(start_node, visited, recStack, path)) {
                // Cycle detected! The path vector contains the cycle.
                // We will select the last node in the cycle (the one that caused it) as the victim.
                if (!path.empty()) {
                    return path.back();
                }
            }
        }
    }
    return 0; // No deadlock
}

bool LockManager::acquireLock(int tx_id, int key) {
    std::unique_lock<std::mutex> lock(lk_mtx);
    
    // If the key is not in the lock table, or is unlocked, we acquire it immediately
    if (lock_table.find(key) == lock_table.end() || lock_table[key] == 0) {
        lock_table[key] = tx_id;
        return true;
    }

    // If we already hold the lock, we are good
    if (lock_table[key] == tx_id) {
        return true;
    }

    // Otherwise, we must wait. Register our wait status.
    tx_waiting_on_key[tx_id] = key;
    wait_table[key].push_back(tx_id);

    // Build graph and check if this wait introduces a deadlock cycle
    int victim = detectDeadlock();
    if (victim != 0) {
        // Deadlock detected! We abort the requester (tx_id) to break the cycle.
        auto& waiters = wait_table[key];
        waiters.erase(std::remove(waiters.begin(), waiters.end(), tx_id), waiters.end());
        tx_waiting_on_key.erase(tx_id);
        
        std::cout << "[Deadlock Detector] Deadlock cycle detected. Choosing requester Tx " 
                  << tx_id << " as victim and aborting." << std::endl;
        return false;
    }

    // No cycle detected, so we block. Create or fetch condition variable.
    if (cv_table.find(tx_id) == cv_table.end()) {
        cv_table[tx_id] = std::make_shared<std::condition_variable>();
    }
    auto cv = cv_table[tx_id];

    // Wait until the lock is freed and we are at the front of the queue, or until we are aborted
    while (lock_table[key] != tx_id) {
        // Wait on the condition variable
        cv->wait(lock);

        // If we are no longer in the waiting table and we don't hold the lock, we must have aborted
        if (tx_waiting_on_key.find(tx_id) == tx_waiting_on_key.end() && lock_table[key] != tx_id) {
            return false;
        }
    }

    // We successfully acquired the lock! Clean up our wait registration.
    tx_waiting_on_key.erase(tx_id);
    return true;
}

void LockManager::releaseLocksForTx(int tx_id) {
    std::lock_guard<std::mutex> lock(lk_mtx);

    // Find all keys locked by this transaction
    std::vector<int> keys_to_release;
    for (const auto& pair : lock_table) {
        if (pair.second == tx_id) {
            keys_to_release.push_back(pair.first);
        }
    }

    // Release them
    for (int key : keys_to_release) {
        lock_table[key] = 0;
        
        // Wake up the next waiting transaction in line (FIFO)
        auto& waiters = wait_table[key];
        // Clean any stale waiters that might have aborted
        waiters.erase(std::remove_if(waiters.begin(), waiters.end(), 
            [this](int waiter_tx) {
                return tx_waiting_on_key.find(waiter_tx) == tx_waiting_on_key.end();
            }), waiters.end());

        if (!waiters.empty()) {
            int next_tx = waiters.front();
            waiters.erase(waiters.begin());
            
            // Assign lock to next transaction
            lock_table[key] = next_tx;
            
            // Wake it up
            if (cv_table.find(next_tx) != cv_table.end()) {
                cv_table[next_tx]->notify_all();
            }
        }
    }

    // Clean wait structures if this transaction is releasing locks due to abort
    tx_waiting_on_key.erase(tx_id);
    cv_table.erase(tx_id);
}

void LockManager::printWFG() {
    std::lock_guard<std::mutex> lock(lk_mtx);
    buildGraph();
    std::cout << "--- Wait-For Graph ---" << std::endl;
    if (wait_for_graph.empty()) {
        std::cout << "  (Graph is empty - no transactions waiting)" << std::endl;
        return;
    }
    for (const auto& pair : wait_for_graph) {
        std::cout << "  Tx " << pair.first << " -> waits for: ";
        for (int holding : pair.second) {
            std::cout << "Tx " << holding << " ";
        }
        std::cout << std::endl;
    }
    std::cout << "----------------------" << std::endl;
}

// =========================================================================
// MVCCDatabase Implementation
// =========================================================================

MVCCDatabase::MVCCDatabase() {}

bool MVCCDatabase::isTxCommitted(int tx_id) const {
    auto it = transactions.find(tx_id);
    if (it != transactions.end()) {
        return it->second.status == TransactionStatus::COMMITTED;
    }
    return false;
}

bool MVCCDatabase::isTxActive(int tx_id) const {
    auto it = transactions.find(tx_id);
    if (it != transactions.end()) {
        return it->second.status == TransactionStatus::ACTIVE;
    }
    return false;
}

bool MVCCDatabase::isVisible(const std::shared_ptr<RecordVersion>& version, const Transaction& tx) const {
    // 1. If created by myself, it is visible
    if (version->xmin == tx.tx_id) {
        // But check if we also deleted it in this transaction
        if (version->xmax == tx.tx_id) {
            return false;
        }
        return true;
    }

    // 2. Created by another transaction. xmin must be committed.
    if (!isTxCommitted(version->xmin)) {
        return false;
    }

    // 3. xmin must have committed BEFORE our transaction started.
    // If xmin was active when we started, it is not visible.
    if (tx.active_at_start.find(version->xmin) != tx.active_at_start.end()) {
        return false;
    }

    // 4. xmin must be older than our transaction ID (cannot see future inserts)
    if (version->xmin > tx.tx_id) {
        return false;
    }

    // 5. Now check deletion (xmax)
    if (version->xmax == 0) {
        // No deletion, so visible!
        return true;
    }

    // If deleted by myself, it's not visible
    if (version->xmax == tx.tx_id) {
        return false;
    }

    // If xmax is not committed, the deletion is not visible (so version is visible)
    if (!isTxCommitted(version->xmax)) {
        return true;
    }

    // If xmax was active when we started, or is in the future, the deletion is not visible
    if (tx.active_at_start.find(version->xmax) != tx.active_at_start.end() || version->xmax > tx.tx_id) {
        return true;
    }

    // Deletion has committed and is visible to our snapshot. Thus, row version is deleted (invisible).
    return false;
}

int MVCCDatabase::beginTransaction() {
    std::lock_guard<std::mutex> lock(db_mtx);
    int tx_id = next_tx_id++;
    
    Transaction tx;
    tx.tx_id = tx_id;
    tx.status = TransactionStatus::ACTIVE;
    
    // Capture snapshot of currently active transaction IDs
    for (const auto& pair : transactions) {
        if (pair.second.status == TransactionStatus::ACTIVE) {
            tx.active_at_start.insert(pair.first);
        }
    }
    
    transactions[tx_id] = tx;
    std::cout << "[Tx Manager] Began Transaction Tx " << tx_id 
              << " (Snapshot active txs: ";
    if (tx.active_at_start.empty()) {
        std::cout << "none";
    } else {
        for (int active_id : tx.active_at_start) std::cout << active_id << " ";
    }
    std::cout << ")" << std::endl;
    
    return tx_id;
}

bool MVCCDatabase::commitTransaction(int tx_id) {
    std::lock_guard<std::mutex> lock(db_mtx);
    auto it = transactions.find(tx_id);
    if (it == transactions.end() || it->second.status != TransactionStatus::ACTIVE) {
        return false;
    }

    it->second.status = TransactionStatus::COMMITTED;
    lock_mgr.releaseLocksForTx(tx_id);
    
    std::cout << "[Tx Manager] Committed Transaction Tx " << tx_id << std::endl;
    return true;
}

void MVCCDatabase::abortTransaction(int tx_id) {
    std::lock_guard<std::mutex> lock(db_mtx);
    auto it = transactions.find(tx_id);
    if (it == transactions.end() || it->second.status != TransactionStatus::ACTIVE) {
        return;
    }

    it->second.status = TransactionStatus::ABORTED;
    std::cout << "[Tx Manager] Aborting Transaction Tx " << tx_id << ". Reverting changes..." << std::endl;

    // Rollback changes
    for (auto& pair : db) {
        Record& rec = pair.second;
        std::shared_ptr<RecordVersion> curr = rec.latest_version;
        std::shared_ptr<RecordVersion> prevNode = nullptr;

        while (curr) {
            bool remove_version = false;
            
            if (curr->xmin == tx_id) {
                // This version was created by our aborted transaction. Discard it.
                remove_version = true;
            } else if (curr->xmax == tx_id) {
                // This version was superseded/deleted by us. Revert xmax back to 0.
                curr->xmax = 0;
            }

            if (remove_version) {
                if (prevNode == nullptr) {
                    // It was the head (latest version)
                    rec.latest_version = curr->prev;
                    curr = rec.latest_version;
                } else {
                    // Link previous node directly to the one before current (prune current)
                    prevNode->prev = curr->prev;
                    curr = curr->prev;
                }
            } else {
                prevNode = curr;
                curr = curr->prev;
            }
        }
    }

    // Release locks and wake up waiters
    lock_mgr.releaseLocksForTx(tx_id);
    std::cout << "[Tx Manager] Rollback complete for Tx " << tx_id << std::endl;
}

bool MVCCDatabase::readRecord(int tx_id, int key, std::string& out_value) {
    Transaction tx;
    {
        std::lock_guard<std::mutex> lock(db_mtx);
        auto it = transactions.find(tx_id);
        if (it == transactions.end() || it->second.status != TransactionStatus::ACTIVE) {
            return false;
        }
        tx = it->second;
    } // Unlock db_mtx

    // Lock-free read! We traverse the version chain to find a visible version
    std::lock_guard<std::mutex> lock(db_mtx);
    auto it = db.find(key);
    if (it == db.end()) {
        return false;
    }

    std::shared_ptr<RecordVersion> curr = it->second.latest_version;
    while (curr) {
        if (isVisible(curr, tx)) {
            out_value = curr->value;
            return true;
        }
        curr = curr->prev;
    }

    return false; // No visible version found
}

bool MVCCDatabase::writeRecord(int tx_id, int key, const std::string& value) {
    // 1. Acquire exclusive lock on key (blocks if held, aborts if deadlock cycle formed)
    if (!lock_mgr.acquireLock(tx_id, key)) {
        abortTransaction(tx_id);
        return false;
    }

    // 2. Perform write / update
    std::lock_guard<std::mutex> lock(db_mtx);
    
    // Check if transaction aborted while we were waiting for the lock
    if (transactions[tx_id].status == TransactionStatus::ABORTED) {
        return false;
    }

    auto it = db.find(key);
    if (it == db.end()) {
        // Insert new record
        Record rec;
        rec.key = key;
        rec.latest_version = std::make_shared<RecordVersion>(value, tx_id);
        db[key] = rec;
        std::cout << "[DB Engine] Tx " << tx_id << " wrote Key " << key 
                  << " = '" << value << "' (Created new record)" << std::endl;
    } else {
        // Update existing record
        Record& rec = it->second;
        std::shared_ptr<RecordVersion> latest = rec.latest_version;

        // Set previous version xmax = tx_id (superseded)
        if (latest) {
            latest->xmax = tx_id;
        }

        // Add new version as the head of the chain
        std::shared_ptr<RecordVersion> new_version = std::make_shared<RecordVersion>(value, tx_id, latest);
        rec.latest_version = new_version;
        std::cout << "[DB Engine] Tx " << tx_id << " updated Key " << key 
                  << " = '" << value << "' (Appended new version to chain)" << std::endl;
    }

    return true;
}

void MVCCDatabase::printDatabaseState() {
    std::lock_guard<std::mutex> lock(db_mtx);
    std::cout << "\n========== CURRENT DATABASE STATE (MVCC Chains) ==========" << std::endl;
    if (db.empty()) {
        std::cout << "  (Database is empty)" << std::endl;
    }
    for (const auto& pair : db) {
        std::cout << "  Key " << pair.first << " version chain:" << std::endl;
        std::shared_ptr<RecordVersion> curr = pair.second.latest_version;
        if (!curr) {
            std::cout << "    [Empty]" << std::endl;
        }
        while (curr) {
            std::cout << "    -> [Value: '" << curr->value 
                      << "' | xmin: " << curr->xmin 
                      << " | xmax: " << (curr->xmax == 0 ? "0 (active)" : std::to_string(curr->xmax)) 
                      << "]" << std::endl;
            curr = curr->prev;
        }
    }
    std::cout << "==========================================================" << std::endl;
    
    // Also print Lock Manager Graph status
    lock_mgr.printWFG();
}
