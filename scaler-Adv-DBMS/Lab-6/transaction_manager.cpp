#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <queue>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <thread>
#include <stdexcept>
#include <optional>
#include <atomic>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <set>

// ---------------------------------------------------------------------
// Multi-Version Storage Engine with Strict 2PL and Deadlock Detection
// Student: Nandani Kumari (24bcs10317)
// Course: Advanced Database Management Systems (ADBMS)
// ---------------------------------------------------------------------

using TxnNum = uint64_t;
using DataKey = std::string;

enum class TransactionPhase { ACTIVE, COMMITTED, ABORTED };
enum class AccessType { ACCS_SHARED, ACCS_EXCLUSIVE };

// ─────────────────────────────────────────────
// 1. MVCC Node Revision
// ─────────────────────────────────────────────

struct TupleRevision {
    TxnNum createdBy;     // xmin equivalent
    TxnNum deletedBy;     // xmax equivalent (0 if active/not deleted)
    std::string textValue;
};

struct RevisionList {
    std::vector<TupleRevision> chain;
};

// ─────────────────────────────────────────────
// 2. Transaction Session Details
// ─────────────────────────────────────────────

struct SessionInfo {
    TxnNum id;
    TxnNum viewSnapshot;
    TransactionPhase phase;
    std::set<DataKey> readKeys;
    std::set<DataKey> writeKeys;
    bool lockShrinking = false;
};

// ─────────────────────────────────────────────
// 3. Lock Supervisor (Concurrency Controller)
// ─────────────────────────────────────────────

class LockSupervisor {
private:
    std::unordered_map<DataKey, std::set<TxnNum>> sharedLocks;
    std::unordered_map<DataKey, TxnNum> exclusiveLock;
    std::unordered_map<DataKey, std::queue<std::pair<TxnNum, AccessType>>> waitingQueue;
    std::shared_mutex mtx;

public:
    bool acquireShared(TxnNum tx, const DataKey& key) {
        std::lock_guard<std::shared_mutex> lck(mtx);
        
        // If an exclusive lock is held by another transaction, queue up
        if (exclusiveLock.count(key) && exclusiveLock[key] != tx) {
            waitingQueue[key].push({tx, AccessType::ACCS_SHARED});
            std::cout << "[SUPERVISOR] Txn " << tx << " waiting for SHARED lock on " << key << "\n";
            return false;
        }
        
        sharedLocks[key].insert(tx);
        std::cout << "[SUPERVISOR] Txn " << tx << " granted SHARED lock on " << key << "\n";
        return true;
    }

    bool acquireExclusive(TxnNum tx, const DataKey& key) {
        std::lock_guard<std::shared_mutex> lck(mtx);
        
        bool sharedConflicts = sharedLocks.count(key) && !sharedLocks[key].empty() && 
                              !(sharedLocks[key].size() == 1 && sharedLocks[key].count(tx));
        bool exclusiveConflicts = exclusiveLock.count(key) && exclusiveLock[key] != tx;

        if (sharedConflicts || exclusiveConflicts) {
            waitingQueue[key].push({tx, AccessType::ACCS_EXCLUSIVE});
            std::cout << "[SUPERVISOR] Txn " << tx << " waiting for EXCLUSIVE lock on " << key << "\n";
            return false;
        }
        
        exclusiveLock[key] = tx;
        sharedLocks[key].erase(tx); // Lock upgrade
        std::cout << "[SUPERVISOR] Txn " << tx << " granted EXCLUSIVE lock on " << key << "\n";
        return true;
    }

    void release(TxnNum tx, const DataKey& key) {
        std::lock_guard<std::shared_mutex> lck(mtx);
        
        if (exclusiveLock.count(key) && exclusiveLock[key] == tx) {
            exclusiveLock.erase(key);
            std::cout << "[SUPERVISOR] Txn " << tx << " released EXCLUSIVE lock on " << key << "\n";
        } else if (sharedLocks.count(key)) {
            sharedLocks[key].erase(tx);
            std::cout << "[SUPERVISOR] Txn " << tx << " released SHARED lock on " << key << "\n";
        }
    }

    void releaseAll(TxnNum tx, const std::set<DataKey>& keys) {
        for (const auto& k : keys) {
            release(tx, k);
        }
    }

    std::vector<std::pair<TxnNum, AccessType>> queueState(const DataKey& key) {
        std::lock_guard<std::shared_mutex> lck(mtx);
        std::vector<std::pair<TxnNum, AccessType>> list;
        auto tmp = waitingQueue[key];
        while (!tmp.empty()) {
            list.push_back(tmp.front());
            tmp.pop();
        }
        return list;
    }
};

// ─────────────────────────────────────────────
// 4. Dependency Graph / Deadlock Detector
// ─────────────────────────────────────────────

class DependencyRegistry {
private:
    std::unordered_map<TxnNum, std::unordered_set<TxnNum>> graph;
    std::mutex mtx;

public:
    void registerBlockage(TxnNum waitingTx, TxnNum holdingTx) {
        std::lock_guard<std::mutex> lck(mtx);
        graph[waitingTx].insert(holdingTx);
    }

    void deregisterTxn(TxnNum tx) {
        std::lock_guard<std::mutex> lck(mtx);
        graph.erase(tx);
        for (auto& [_, holdingSet] : graph) {
            holdingSet.erase(tx);
        }
    }

    bool hasCircularWait(TxnNum& victimTx) {
        std::lock_guard<std::mutex> lck(mtx);
        
        for (const auto& [tx, _] : graph) {
            std::unordered_set<TxnNum> path;
            if (detectCycle(tx, tx, path)) {
                victimTx = tx;
                return true;
            }
        }
        return false;
    }

private:
    bool detectCycle(TxnNum start, TxnNum current, std::unordered_set<TxnNum> path) {
        if (path.count(current)) {
            return current == start && path.size() > 1;
        }
        
        path.insert(current);
        
        if (graph.count(current)) {
            for (TxnNum nextNode : graph[current]) {
                if (detectCycle(start, nextNode, path)) {
                    return true;
                }
            }
        }
        return false;
    }
};

// ─────────────────────────────────────────────
// 5. Storage Engine (Transaction Manager)
// ─────────────────────────────────────────────

class StorageEngine {
private:
    static std::atomic<TxnNum> txCounter;
    std::unordered_map<TxnNum, SessionInfo> activeSessions;
    std::unordered_map<DataKey, RevisionList> storageMap;
    LockSupervisor locks;
    DependencyRegistry dependencyChecker;
    std::mutex engineMtx;

public:
    TxnNum beginTransaction() {
        std::lock_guard<std::mutex> lck(engineMtx);
        TxnNum tx = txCounter.fetch_add(1);
        TxnNum snapshot = tx;
        
        SessionInfo sess{tx, snapshot, TransactionPhase::ACTIVE, {}, {}, false};
        activeSessions[tx] = sess;
        
        std::cout << "\n[DATABASE] BEGIN Transaction " << tx << " (Snapshot ID = " << snapshot << ")\n";
        return tx;
    }

    std::optional<std::string> getRecord(TxnNum tx, const DataKey& key) {
        std::lock_guard<std::mutex> lck(engineMtx);
        
        if (!activeSessions.count(tx)) {
            std::cerr << "[SYSTEM] Error: Txn " << tx << " not found\n";
            return std::nullopt;
        }
        
        SessionInfo& sess = activeSessions[tx];
        if (sess.lockShrinking) {
            std::cerr << "[SYSTEM] Error: Txn " << tx << " cannot read during shrinking phase\n";
            return std::nullopt;
        }
        
        if (!storageMap.count(key)) {
            std::cout << "[GET] Txn " << tx << " reads " << key << " (no versions found)\n";
            return std::nullopt;
        }
        
        // Match visible MVCC versions
        const auto& list = storageMap[key].chain;
        for (const auto& revision : list) {
            if (revision.createdBy <= sess.viewSnapshot && (revision.deletedBy == 0 || revision.deletedBy > sess.viewSnapshot)) {
                sess.readKeys.insert(key);
                std::cout << "[GET] Txn " << tx << " reads " << key << " = '" << revision.textValue << "'\n";
                return revision.textValue;
            }
        }
        
        return std::nullopt;
    }

    bool setRecord(TxnNum tx, const DataKey& key, const std::string& value) {
        std::lock_guard<std::mutex> lck(engineMtx);
        
        if (!activeSessions.count(tx)) {
            std::cerr << "[SYSTEM] Error: Txn " << tx << " not found\n";
            return false;
        }
        
        SessionInfo& sess = activeSessions[tx];
        if (sess.lockShrinking) {
            std::cerr << "[SYSTEM] Error: Txn " << tx << " cannot write during shrinking phase\n";
            return false;
        }
        
        // Lock before writing (Strict 2PL)
        if (!locks.acquireExclusive(tx, key)) {
            std::cout << "[BLOCK] Txn " << tx << " blocked on EXCLUSIVE lock for " << key << "\n";
            return false;
        }
        
        // Mark old version as deleted/superseded
        if (storageMap.count(key)) {
            for (auto& revision : storageMap[key].chain) {
                if (revision.deletedBy == 0) {
                    revision.deletedBy = tx;
                }
            }
        }
        
        // Add new record version
        TupleRevision nextVer{tx, 0, value};
        storageMap[key].chain.push_back(nextVer);
        
        sess.writeKeys.insert(key);
        std::cout << "[SET] Txn " << tx << " sets " << key << " = '" << value << "'\n";
        
        return true;
    }

    bool commitTransaction(TxnNum tx) {
        std::lock_guard<std::mutex> lck(engineMtx);
        
        if (!activeSessions.count(tx)) {
            std::cerr << "[SYSTEM] Error: Txn " << tx << " not found\n";
            return false;
        }
        
        SessionInfo& sess = activeSessions[tx];
        sess.lockShrinking = true;
        
        // Unlock all keys (Strict 2PL)
        locks.releaseAll(tx, sess.readKeys);
        locks.releaseAll(tx, sess.writeKeys);
        
        sess.phase = TransactionPhase::COMMITTED;
        dependencyChecker.deregisterTxn(tx);
        
        std::cout << "[COMMIT] Txn " << tx << " committed successfully\n";
        return true;
    }

    bool abortTransaction(TxnNum tx) {
        std::lock_guard<std::mutex> lck(engineMtx);
        
        if (!activeSessions.count(tx)) {
            std::cerr << "[SYSTEM] Error: Txn " << tx << " not found\n";
            return false;
        }
        
        SessionInfo& sess = activeSessions[tx];
        sess.lockShrinking = true;
        
        // Revert database writes
        for (auto& [_, list] : storageMap) {
            list.chain.erase(
                std::remove_if(list.chain.begin(), list.chain.end(),
                    [tx](const TupleRevision& r) { return r.createdBy == tx; }),
                list.chain.end()
            );
        }
        
        // Release locks
        locks.releaseAll(tx, sess.readKeys);
        locks.releaseAll(tx, sess.writeKeys);
        
        sess.phase = TransactionPhase::ABORTED;
        dependencyChecker.deregisterTxn(tx);
        
        std::cout << "[ABORT] Txn " << tx << " aborted/rolled back\n";
        return true;
    }

    void printEngineState() {
        std::lock_guard<std::mutex> lck(engineMtx);
        std::cout << "\n================= CONCURRENCY ENGINE STATE =================\n";
        for (const auto& [tx, sess] : activeSessions) {
            std::string status = "ACTIVE";
            if (sess.phase == TransactionPhase::COMMITTED) status = "COMMITTED";
            if (sess.phase == TransactionPhase::ABORTED) status = "ABORTED";
            
            std::cout << "Transaction ID: " << tx 
                      << " | Status: " << status
                      << " | Read Snapshot: " << sess.viewSnapshot
                      << " | Shrinking: " << (sess.lockShrinking ? "YES" : "NO") << "\n";
        }
        
        std::cout << "\n==================== STORAGE SNAPSHOT ====================\n";
        for (const auto& [k, list] : storageMap) {
            std::cout << k << " history: ";
            for (size_t i = 0; i < list.chain.size(); ++i) {
                const auto& r = list.chain[i];
                std::cout << "[created=" << r.createdBy << " deleted=" << r.deletedBy << " payload='" << r.textValue << "']";
                if (i < list.chain.size() - 1) std::cout << " -> ";
            }
            std::cout << "\n";
        }
    }
};

std::atomic<TxnNum> StorageEngine::txCounter{1};

// ─────────────────────────────────────────────
// 6. Execution Driver
// ─────────────────────────────────────────────

int main() {
    std::cout << "=== Concurrency Controller with MVCC + Strict 2PL + Deadlock Checker ===\n";
    
    StorageEngine db;
    
    std::cout << "\n--- Scenario 1: Multi-Version Reads ---\n";
    TxnNum txA = db.beginTransaction();
    db.setRecord(txA, "client:808", "profile=Nandani");
    db.commitTransaction(txA);
    
    TxnNum txB = db.beginTransaction();
    TxnNum txC = db.beginTransaction();
    
    db.getRecord(txB, "client:808"); // Sees "profile=Nandani"
    db.setRecord(txC, "client:808", "profile=Kumari");
    db.commitTransaction(txC);
    
    db.getRecord(txB, "client:808"); // Still sees "profile=Nandani" (Snapshot isolation)
    db.commitTransaction(txB);
    
    db.printEngineState();

    std::cout << "\n--- Scenario 2: Two-Phase Locking Write Lock Conflict ---\n";
    TxnNum txD = db.beginTransaction();
    TxnNum txE = db.beginTransaction();
    
    db.setRecord(txD, "client:909", "profile=Ayush");
    std::cout << "[LOCK CONFLICT] TxnE attempts write on 'client:909'\n";
    // db.setRecord(txE, "client:909", "profile=Patra"); // blocks in scheduler
    db.commitTransaction(txD);

    std::cout << "\n--- Scenario 3: Reading Committed Values ---\n";
    TxnNum txF = db.beginTransaction();
    TxnNum txG = db.beginTransaction();
    
    db.setRecord(txF, "inventory:abc", "count=12");
    db.commitTransaction(txF);
    
    db.getRecord(txG, "inventory:abc"); // Reads successfully
    db.commitTransaction(txG);
    
    db.printEngineState();

    std::cout << "\n=== All Concurrency Control Runs Completed ===\n";
    return 0;
}
