#include <iostream>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>

// --- MVCC Implementation ---
// A version chain for a specific key
struct RecordVersion {
    int txn_id;        // The transaction that created this version (xmin)
    int value;
    RecordVersion* prev; // Pointer to older version
};

class MVCCStore {
private:
    std::unordered_map<std::string, RecordVersion*> store;

public:
    void write(const std::string& key, int value, int txn_id) {
        RecordVersion* new_version = new RecordVersion{txn_id, value, nullptr};
        if (store.find(key) != store.end()) {
            new_version->prev = store[key]; // Link to older version
        }
        store[key] = new_version;
        std::cout << "Txn " << txn_id << " wrote " << key << " = " << value << std::endl;
    }

    int read(const std::string& key, int current_txn_id) {
        if (store.find(key) == store.end()) return -1; // Not found

        RecordVersion* curr = store[key];
        // Simplified Snapshot Isolation: Find the latest version created by a transaction
        // that is less than or equal to current_txn_id (assuming sequential txn IDs).
        while (curr != nullptr) {
            if (curr->txn_id <= current_txn_id) {
                return curr->value;
            }
            curr = curr->prev;
        }
        return -1;
    }
};


// --- Strict 2PL Lock Manager with Deadlock Detection ---
enum class LockType { SHARED, EXCLUSIVE };

struct LockRequest {
    int txn_id;
    LockType type;
};

class LockManager {
private:
    // Key -> List of granted/waiting locks
    std::unordered_map<std::string, std::vector<LockRequest>> lock_table;
    
    // Wait-For Graph: txn_id -> set of txn_ids it is waiting for
    std::unordered_map<int, std::unordered_set<int>> wait_for_graph;

    bool hasCycle(int curr, std::unordered_set<int>& visited, std::unordered_set<int>& recStack) {
        if (!visited.count(curr)) {
            visited.insert(curr);
            recStack.insert(curr);

            for (int neighbor : wait_for_graph[curr]) {
                if (!visited.count(neighbor) && hasCycle(neighbor, visited, recStack)) {
                    return true;
                } else if (recStack.count(neighbor)) {
                    return true;
                }
            }
        }
        recStack.erase(curr);
        return false;
    }

public:
    bool detectDeadlock() {
        std::unordered_set<int> visited;
        std::unordered_set<int> recStack;
        for (const auto& pair : wait_for_graph) {
            if (hasCycle(pair.first, visited, recStack)) {
                return true;
            }
        }
        return false;
    }

    bool acquireLock(int txn_id, const std::string& key, LockType type) {
        auto& queue = lock_table[key];
        
        // If queue is empty, grant lock immediately
        if (queue.empty()) {
            queue.push_back({txn_id, type});
            std::cout << "Txn " << txn_id << " acquired " << (type == LockType::SHARED ? "S" : "X") << "-Lock on " << key << std::endl;
            return true;
        }

        // Simplistic check: If exclusive lock is held, or requesting exclusive and there are any locks, must wait
        bool conflict = false;
        for (const auto& req : queue) {
            if (req.type == LockType::EXCLUSIVE || type == LockType::EXCLUSIVE) {
                conflict = true;
                wait_for_graph[txn_id].insert(req.txn_id); // Add edge to Wait-For graph
            }
        }

        if (conflict) {
            std::cout << "Txn " << txn_id << " waiting for lock on " << key << std::endl;
            if (detectDeadlock()) {
                std::cout << "--> DEADLOCK DETECTED! Aborting Txn " << txn_id << std::endl;
                wait_for_graph.erase(txn_id);
                return false; // Transaction must abort
            }
            return false; // Conceptually blocks here in a real system
        }

        queue.push_back({txn_id, type});
        std::cout << "Txn " << txn_id << " acquired S-Lock on " << key << std::endl;
        return true;
    }

    void releaseLocks(int txn_id) {
        std::cout << "Txn " << txn_id << " releasing locks (Strict 2PL Phase 2)" << std::endl;
        for (auto& pair : lock_table) {
            auto& queue = pair.second;
            // Remove locks for this txn
            for (auto it = queue.begin(); it != queue.end(); ) {
                if (it->txn_id == txn_id) {
                    it = queue.erase(it);
                } else {
                    ++it;
                }
            }
        }
        // Remove from WFG
        wait_for_graph.erase(txn_id);
        for (auto& pair : wait_for_graph) {
            pair.second.erase(txn_id);
        }
    }
};

int main() {
    std::cout << "--- Lab 6: Transaction Manager (MVCC & S2PL) ---" << std::endl;
    
    MVCCStore store;
    LockManager lock_mgr;

    // 1. MVCC Demonstration
    std::cout << "\n[MVCC Demonstration]" << std::endl;
    store.write("Balance", 100, 1); // Txn 1 writes
    store.write("Balance", 200, 2); // Txn 2 writes (creates new version)
    
    std::cout << "Txn 1 reading Balance: " << store.read("Balance", 1) << std::endl; // Should read 100
    std::cout << "Txn 2 reading Balance: " << store.read("Balance", 2) << std::endl; // Should read 200

    // 2. Strict 2PL & Deadlock Detection Demonstration
    std::cout << "\n[Deadlock Detection Demonstration]" << std::endl;
    
    // Txn 10 acquires X-lock on A
    lock_mgr.acquireLock(10, "A", LockType::EXCLUSIVE);
    
    // Txn 11 acquires X-lock on B
    lock_mgr.acquireLock(11, "B", LockType::EXCLUSIVE);
    
    // Txn 10 wants X-lock on B (Waits for Txn 11)
    lock_mgr.acquireLock(10, "B", LockType::EXCLUSIVE);
    
    // Txn 11 wants X-lock on A (Waits for Txn 10 -> DEADLOCK)
    lock_mgr.acquireLock(11, "A", LockType::EXCLUSIVE);

    // Release locks (Commit/Abort)
    lock_mgr.releaseLocks(10);
    lock_mgr.releaseLocks(11);

    return 0;
}
