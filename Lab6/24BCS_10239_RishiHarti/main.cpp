#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <chrono>
#include <queue>
#include <algorithm>
#include <stdexcept>
#include <iomanip>
#include <cassert>

// ============================================================================
// PART 1: TRANSACTION METADATA & STATE
// ============================================================================

enum class TxState { ACTIVE, COMMITTED, ABORTED };

std::string txStateToString(TxState state) {
    switch (state) {
        case TxState::ACTIVE: return "\033[33mACTIVE\033[0m";
        case TxState::COMMITTED: return "\033[32mCOMMITTED\033[0m";
        case TxState::ABORTED: return "\033[31mABORTED\033[0m";
    }
    return "UNKNOWN";
}

struct Transaction {
    int txid;
    TxState state;
    std::vector<int> acquired_locks; // Track locks held for Strict 2PL release
};

// ============================================================================
// PART 2: MVCC STORAGE ENGINE
// ============================================================================

struct RowVersion {
    std::string value;
    int txid_created;
    int txid_expired;
    RowVersion* prev; // Pointer to older version in the version chain

    RowVersion(std::string val, int created, RowVersion* p = nullptr)
        : value(std::move(val)), txid_created(created), txid_expired(0), prev(p) {}
};

struct Row {
    int row_id;
    RowVersion* head; // Points to the newest version in the chain

    explicit Row(int id, const std::string& initial_val, int txid) {
        row_id = id;
        head = new RowVersion(initial_val, txid);
    }

    ~Row() {
        RowVersion* curr = head;
        while (curr != nullptr) {
            RowVersion* temp = curr;
            curr = curr->prev;
            delete temp;
        }
    }
};

class MVCCStorage {
private:
    std::unordered_map<int, Row*> table;
    std::unordered_map<int, TxState> tx_registry; // txid -> state
    std::mutex db_mutex;

    bool isTxCommitted(int txid) {
        if (txid == 0) return true; // System/Initial tx
        auto it = tx_registry.find(txid);
        if (it == tx_registry.end()) return false;
        return it->second == TxState::COMMITTED;
    }

    bool isTxActive(int txid) {
        auto it = tx_registry.find(txid);
        if (it == tx_registry.end()) return false;
        return it->second == TxState::ACTIVE;
    }

public:
    MVCCStorage() {
        tx_registry[0] = TxState::COMMITTED; // Bootstrapping transaction
    }

    ~MVCCStorage() {
        for (auto& pair : table) {
            delete pair.second;
        }
    }

    void registerTx(int txid) {
        std::lock_guard<std::mutex> lock(db_mutex);
        tx_registry[txid] = TxState::ACTIVE;
    }

    void updateTxState(int txid, TxState state) {
        std::lock_guard<std::mutex> lock(db_mutex);
        tx_registry[txid] = state;
    }

    void insertInitialRow(int row_id, const std::string& initial_val) {
        std::lock_guard<std::mutex> lock(db_mutex);
        table[row_id] = new Row(row_id, initial_val, 0);
    }

    // MVCC Snapshot Isolation read: latest committed version relative to reader txid
    std::string read(int txid, int row_id) {
        std::lock_guard<std::mutex> lock(db_mutex);
        
        auto it = table.find(row_id);
        if (it == table.end()) {
            throw std::runtime_error("MVCC Error: Row " + std::to_string(row_id) + " not found.");
        }

        RowVersion* curr = it->second->head;
        while (curr != nullptr) {
            bool visible = false;

            // Visibility Rule:
            // 1. Version was created by a committed transaction OR by the reading transaction itself
            if (curr->txid_created == txid || isTxCommitted(curr->txid_created)) {
                // AND
                // 2. Created before or at the current transaction ID
                if (curr->txid_created <= txid) {
                    // AND
                    // 3. Has NOT been expired yet, or expired by an uncommitted transaction, or expired after reader started
                    if (curr->txid_expired == 0) {
                        visible = true;
                    } else if (curr->txid_expired > txid) {
                        visible = true; // Expired by future transaction
                    } else if (!isTxCommitted(curr->txid_expired) && curr->txid_expired != txid) {
                        visible = true; // Expired by active/aborted transaction
                    }
                }
            }

            if (visible) {
                return curr->value;
            }
            curr = curr->prev; // Traverse chain to older version
        }

        throw std::runtime_error("MVCC Error: No visible version found for row " + std::to_string(row_id));
    }

    void write(int txid, int row_id, const std::string& val) {
        std::lock_guard<std::mutex> lock(db_mutex);
        
        auto it = table.find(row_id);
        if (it == table.end()) {
            throw std::runtime_error("MVCC Error: Row " + std::to_string(row_id) + " not found.");
        }

        Row* row = it->second;
        RowVersion* old_head = row->head;
        
        // Append new version to version chain
        RowVersion* new_version = new RowVersion(val, txid, old_head);
        row->head = new_version;
        
        // Mark old version as expired by current txid
        old_head->txid_expired = txid;
    }

    void printVersionChain(int row_id) {
        std::lock_guard<std::mutex> lock(db_mutex);
        auto it = table.find(row_id);
        if (it == table.end()) return;

        std::cout << "Version chain for Row [" << row_id << "]: ";
        RowVersion* curr = it->second->head;
        while (curr != nullptr) {
            std::cout << "(\"" << curr->value << "\", CreatedBy: Tx " << curr->txid_created 
                      << ", ExpiredBy: " << (curr->txid_expired == 0 ? "None" : "Tx " + std::to_string(curr->txid_expired)) << ")";
            if (curr->prev) std::cout << " -> ";
            curr = curr->prev;
        }
        std::cout << std::endl;
    }
};

// ============================================================================
// PART 3: LOCK MANAGER (STRICT 2PL) & DEADLOCK DETECTION
// ============================================================================

class LockManager {
private:
    // Lock States
    std::unordered_map<int, int> lock_owners;                      // row_id -> txid (X-Lock Owner)
    std::unordered_map<int, std::queue<int>> lock_wait_queues;     // row_id -> queue of waiting txids
    std::unordered_map<int, std::vector<int>> tx_acquired_locks;   // txid -> list of row_ids locked
    
    // Waits-For Graph (Deadlock Graph)
    std::unordered_map<int, std::unordered_set<int>> waits_for;    // txid (waiting) -> set of txids (holding)

    std::mutex lock_mutex;
    std::unordered_map<int, std::condition_variable> tx_cvs;       // txid -> CV to block on wait

    bool dfsCheckCycle(int curr, std::unordered_set<int>& visited, std::unordered_set<int>& stack) {
        if (stack.find(curr) != stack.end()) {
            return true; // Cycle detected
        }
        if (visited.find(curr) != visited.end()) {
            return false;
        }

        visited.insert(curr);
        stack.insert(curr);

        auto it = waits_for.find(curr);
        if (it != waits_for.end()) {
            for (int neighbor : it->second) {
                if (dfsCheckCycle(neighbor, visited, stack)) {
                    return true;
                }
            }
        }

        stack.erase(curr);
        return false;
    }

    bool hasDeadlockCycle() {
        std::unordered_set<int> visited;
        std::unordered_set<int> stack;

        for (const auto& pair : waits_for) {
            if (dfsCheckCycle(pair.first, visited, stack)) {
                return true;
            }
        }
        return false;
    }

public:
    LockManager() = default;

    bool acquireExclusiveLock(int txid, int row_id) {
        std::unique_lock<std::mutex> lock(lock_mutex);

        auto owner_it = lock_owners.find(row_id);
        
        // Case 1: Row is unlocked -> Acquire immediately
        if (owner_it == lock_owners.end() || owner_it->second == 0) {
            lock_owners[row_id] = txid;
            tx_acquired_locks[txid].push_back(row_id);
            std::cout << "[\033[94mLockMgr\033[0m] Tx " << txid << " successfully acquired exclusive lock on Row " << row_id << std::endl;
            return true;
        }

        // Case 2: Row is already locked by the requestor itself
        if (owner_it->second == txid) {
            return true;
        }

        // Case 3: Row is locked by another transaction -> Wait & Deadlock check
        int active_owner = owner_it->second;
        std::cout << "[\033[94mLockMgr\033[0m] Tx " << txid << " blocked on Row " << row_id 
                  << " (held by Tx " << active_owner << "). Registering Waits-For dependency..." << std::endl;

        lock_wait_queues[row_id].push(txid);
        waits_for[txid].insert(active_owner);

        // Deadlock Cycle Detection
        if (hasDeadlockCycle()) {
            std::cout << "[\033[31mDEADLOCK DETECTED\033[0m] Waits-For cycle detected. Proactively aborting requesting Tx " << txid << " to break the cycle." << std::endl;
            
            // Cleanup wait state
            waits_for[txid].erase(active_owner);
            
            // Remove from wait queue
            auto& q = lock_wait_queues[row_id];
            std::queue<int> temp_q;
            while (!q.empty()) {
                if (q.front() != txid) temp_q.push(q.front());
                q.pop();
            }
            lock_wait_queues[row_id] = temp_q;

            return false; // Returns false to signal abortion
        }

        // Block transaction until woken up
        tx_cvs[txid].wait(lock, [this, txid, row_id]() {
            return lock_owners[row_id] == txid;
        });

        // Woken up and lock assigned
        return true;
    }

    // Strict 2PL release: release all locks only at Transaction End
    void releaseAllLocks(int txid) {
        std::lock_guard<std::mutex> lock(lock_mutex);

        auto it = tx_acquired_locks.find(txid);
        if (it == tx_acquired_locks.end()) return;

        std::cout << "[\033[94mLockMgr\033[0m] Releasing all locks held by Tx " << txid << " (Strict 2PL)..." << std::endl;
        
        for (int row_id : it->second) {
            auto& wait_queue = lock_wait_queues[row_id];
            
            if (!wait_queue.empty()) {
                // Assign lock to next waiting transaction (Strict 2PL queue progression)
                int next_txid = wait_queue.front();
                wait_queue.pop();

                lock_owners[row_id] = next_txid;
                tx_acquired_locks[next_txid].push_back(row_id);

                // Update Waits-For Graph (remove incoming edge, redirect wait queue dependencies if any)
                waits_for[next_txid].erase(txid);
                
                std::cout << "[\033[94mLockMgr\033[0m] Lock on Row " << row_id << " passed from Tx " 
                          << txid << " to Tx " << next_txid << ". Waking up Tx " << next_txid << std::endl;

                tx_cvs[next_txid].notify_one();
            } else {
                // Free the lock
                lock_owners.erase(row_id);
            }
        }

        tx_acquired_locks.erase(txid);
        waits_for.erase(txid);
        tx_cvs.erase(txid);
    }
};

// ============================================================================
// PART 4: SYSTEM TRANSACTION RUNNER & SIMULATOR
// ============================================================================

MVCCStorage db;
LockManager lock_mgr;

void runTransaction1() {
    int txid = 1;
    db.registerTx(txid);
    std::cout << "[Tx 1] Started." << std::endl;

    // Tx 1 acquires lock on Row A (ID: 101)
    if (!lock_mgr.acquireExclusiveLock(txid, 101)) {
        db.updateTxState(txid, TxState::ABORTED);
        lock_mgr.releaseAllLocks(txid);
        return;
    }

    db.write(txid, 101, "A_Version_1_Tx1");
    std::cout << "[Tx 1] Wrote Row 101: \"A_Version_1_Tx1\"" << std::endl;

    // Sleep to simulate prolonged active state
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    db.updateTxState(txid, TxState::COMMITTED);
    std::cout << "[Tx 1] Committed." << std::endl;
    lock_mgr.releaseAllLocks(txid);
}

void runTransaction2() {
    // Wait slightly so Tx 1 starts first
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    int txid = 2;
    db.registerTx(txid);
    std::cout << "[Tx 2] Started." << std::endl;

    // Read Row A (MVCC Snapshot Isolation read: should see Initial state even though Tx1 is writing)
    std::string val = db.read(txid, 101);
    std::cout << "[Tx 2] MVCC Isolation Read Row 101: \"" << val << "\" (Should see committed \"Initial_A\")" << std::endl;

    // Try to acquire lock on Row A (ID: 101) to write
    // This will block because Tx 1 holds the lock!
    std::cout << "[Tx 2] Attempting to acquire lock on Row 101 to update..." << std::endl;
    if (!lock_mgr.acquireExclusiveLock(txid, 101)) {
        db.updateTxState(txid, TxState::ABORTED);
        std::cout << "[Tx 2] Aborted." << std::endl;
        lock_mgr.releaseAllLocks(txid);
        return;
    }

    // Woken up when Tx 1 commits and releases lock
    db.write(txid, 101, "A_Version_2_Tx2");
    std::cout << "[Tx 2] Woke up. Wrote Row 101: \"A_Version_2_Tx2\"" << std::endl;

    db.updateTxState(txid, TxState::COMMITTED);
    std::cout << "[Tx 2] Committed." << std::endl;
    lock_mgr.releaseAllLocks(txid);
}

// Deadlock Scenario Simulator Transactions
void runDeadlockTx4() {
    int txid = 4;
    db.registerTx(txid);
    std::cout << "[Tx 4] Started." << std::endl;

    // Lock Row A (101)
    if (!lock_mgr.acquireExclusiveLock(txid, 101)) {
        db.updateTxState(txid, TxState::ABORTED);
        std::cout << "[Tx 4] Aborted." << std::endl;
        lock_mgr.releaseAllLocks(txid);
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Try to lock Row B (102) -> will block because Tx 5 has it
    std::cout << "[Tx 4] Requesting Row 102..." << std::endl;
    if (!lock_mgr.acquireExclusiveLock(txid, 102)) {
        db.updateTxState(txid, TxState::ABORTED);
        std::cout << "[Tx 4] Aborted." << std::endl;
        lock_mgr.releaseAllLocks(txid);
        return;
    }

    db.write(txid, 102, "Tx4_Done");
    db.updateTxState(txid, TxState::COMMITTED);
    std::cout << "[Tx 4] Committed." << std::endl;
    lock_mgr.releaseAllLocks(txid);
}

void runDeadlockTx5() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    int txid = 5;
    db.registerTx(txid);
    std::cout << "[Tx 5] Started." << std::endl;

    // Lock Row B (102)
    if (!lock_mgr.acquireExclusiveLock(txid, 102)) {
        db.updateTxState(txid, TxState::ABORTED);
        std::cout << "[Tx 5] Aborted." << std::endl;
        lock_mgr.releaseAllLocks(txid);
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Try to lock Row A (101) -> Creates Deadlock Cycle: Tx 5 waits for Tx 4 (on 101), Tx 4 waits for Tx 5 (on 102).
    // LockManager will detect cycle and abort Tx 5 immediately!
    std::cout << "[Tx 5] Requesting Row 101 (creating deadlock loop)..." << std::endl;
    if (!lock_mgr.acquireExclusiveLock(txid, 101)) {
        db.updateTxState(txid, TxState::ABORTED);
        std::cout << "[Tx 5] Aborted by deadlock resolver." << std::endl;
        lock_mgr.releaseAllLocks(txid);
        return;
    }

    db.write(txid, 101, "Tx5_Done");
    db.updateTxState(txid, TxState::COMMITTED);
    std::cout << "[Tx 5] Committed." << std::endl;
    lock_mgr.releaseAllLocks(txid);
}

int main() {
    std::cout << "==========================================================" << std::endl;
    std::cout << "    LAB 6: MVCC ENGINE + STRICT 2PL + DEADLOCK DETECTION  " << std::endl;
    std::cout << "    Roll No: 24BCS10239 | Name: Rishi Harti" << std::endl;
    std::cout << "==========================================================" << std::endl;

    // Initialize Database Rows
    db.insertInitialRow(101, "Initial_A");
    db.insertInitialRow(102, "Initial_B");

    std::cout << "\n\033[36m--- SCENARIO 1: MVCC SNAPSHOT ISOLATION & WRITE-WRITE LOCK BLOCKING ---\033[0m" << std::endl;
    db.printVersionChain(101);

    std::thread t1(runTransaction1);
    std::thread t2(runTransaction2);

    t1.join();
    t2.join();

    std::cout << "\n[+] State of row version chains post Scenario 1:" << std::endl;
    db.printVersionChain(101);

    // Read Row A at final/system tx context
    std::cout << "[System Read] Row 101 visible version: \"" << db.read(100, 101) << "\"" << std::endl;

    std::cout << "\n\033[36m--- SCENARIO 2: DEADLOCK CYCLE DETECTION AND RESOLUTION ---\033[0m" << std::endl;
    
    std::thread t4(runDeadlockTx4);
    std::thread t5(runDeadlockTx5);

    t4.join();
    t5.join();

    std::cout << "\n\033[92m[+] All simulations finished successfully! Transaction Manager is robust.\033[0m\n" << std::endl;
    return 0;
}
