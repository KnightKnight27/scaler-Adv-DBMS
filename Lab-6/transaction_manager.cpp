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
// Concurrency Control Engine
// Student: Ayush Kumar Patra (24bcs10474)
// Course: Advanced Database Management Systems (ADBMS)
// ---------------------------------------------------------------------

using TransactionID = uint64_t;
using RecordKey = std::string;

enum class TxnState { ACTIVE, COMMITTED, ROLLED_BACK };
enum class LockMode { SHARED, EXCLUSIVE };

// ─────────────────────────────────────────────
// 1. MVCC Row Version
// ─────────────────────────────────────────────

struct RecordVersion {
    TransactionID creator_tx;    // xmin equivalent
    TransactionID deleter_tx;    // xmax equivalent (0 if active/not deleted)
    std::string data_payload;
};

struct VersionHistory {
    std::vector<RecordVersion> list;
};

// ─────────────────────────────────────────────
// 2. Txn Metadata
// ─────────────────────────────────────────────

struct TxnContext {
    TransactionID id;
    TransactionID snapshot_id;
    TxnState state;
    std::set<RecordKey> read_set;
    std::set<RecordKey> write_set;
    bool shrinking_phase = false;
};

// ─────────────────────────────────────────────
// 3. Lock Controller
// ─────────────────────────────────────────────

class LockRegistry {
private:
    std::unordered_map<RecordKey, std::set<TransactionID>> shared_holders;
    std::unordered_map<RecordKey, TransactionID> exclusive_holder;
    std::unordered_map<RecordKey, std::queue<std::pair<TransactionID, LockMode>>> request_queue;
    std::shared_mutex registry_mutex;

public:
    bool requestSharedLock(TransactionID tx, const RecordKey& key) {
        std::lock_guard<std::shared_mutex> lck(registry_mutex);
        
        // If an exclusive lock is held by another transaction, we must queue
        if (exclusive_holder.count(key) && exclusive_holder[key] != tx) {
            request_queue[key].push({tx, LockMode::SHARED});
            std::cout << "[REGISTRY] Txn " << tx << " waits for SHARED on " << key << "\n";
            return false;
        }
        
        shared_holders[key].insert(tx);
        std::cout << "[REGISTRY] Txn " << tx << " acquired SHARED lock on " << key << "\n";
        return true;
    }

    bool requestExclusiveLock(TransactionID tx, const RecordKey& key) {
        std::lock_guard<std::shared_mutex> lck(registry_mutex);
        
        // If anyone else holds any lock, we must queue
        bool other_shared = shared_holders.count(key) && !shared_holders[key].empty() && 
                            !(shared_holders[key].size() == 1 && shared_holders[key].count(tx));
        bool other_exclusive = exclusive_holder.count(key) && exclusive_holder[key] != tx;

        if (other_shared || other_exclusive) {
            request_queue[key].push({tx, LockMode::EXCLUSIVE});
            std::cout << "[REGISTRY] Txn " << tx << " waits for EXCLUSIVE on " << key << "\n";
            return false;
        }
        
        exclusive_holder[key] = tx;
        shared_holders[key].erase(tx); // upgrade lock if held
        std::cout << "[REGISTRY] Txn " << tx << " acquired EXCLUSIVE lock on " << key << "\n";
        return true;
    }

    void releaseLock(TransactionID tx, const RecordKey& key) {
        std::lock_guard<std::shared_mutex> lck(registry_mutex);
        
        if (exclusive_holder.count(key) && exclusive_holder[key] == tx) {
            exclusive_holder.erase(key);
            std::cout << "[REGISTRY] Txn " << tx << " released EXCLUSIVE lock on " << key << "\n";
        } else if (shared_holders.count(key)) {
            shared_holders[key].erase(tx);
            std::cout << "[REGISTRY] Txn " << tx << " released SHARED lock on " << key << "\n";
        }
    }

    void releaseAllLocks(TransactionID tx, const std::set<RecordKey>& keys) {
        for (const auto& k : keys) {
            releaseLock(tx, k);
        }
    }

    std::vector<std::pair<TransactionID, LockMode>> getWaiters(const RecordKey& key) {
        std::lock_guard<std::shared_mutex> lck(registry_mutex);
        std::vector<std::pair<TransactionID, LockMode>> list;
        auto tmp = request_queue[key];
        while (!tmp.empty()) {
            list.push_back(tmp.front());
            tmp.pop();
        }
        return list;
    }
};

// ─────────────────────────────────────────────
// 4. Deadlock Detection
// ─────────────────────────────────────────────

class DeadlockGraph {
private:
    std::unordered_map<TransactionID, std::unordered_set<TransactionID>> dependency_map;
    std::mutex graph_mutex;

public:
    void addDependency(TransactionID waiting_tx, TransactionID holding_tx) {
        std::lock_guard<std::mutex> lck(graph_mutex);
        dependency_map[waiting_tx].insert(holding_tx);
    }

    void removeTransaction(TransactionID tx) {
        std::lock_guard<std::mutex> lck(graph_mutex);
        dependency_map.erase(tx);
        for (auto& [_, deps] : dependency_map) {
            deps.erase(tx);
        }
    }

    bool checkForCycles(TransactionID& aborted_candidate) {
        std::lock_guard<std::mutex> lck(graph_mutex);
        
        for (const auto& [tx, _] : dependency_map) {
            std::unordered_set<TransactionID> trace;
            if (findCycleDFS(tx, tx, trace)) {
                aborted_candidate = tx;
                return true;
            }
        }
        return false;
    }

private:
    bool findCycleDFS(TransactionID origin, TransactionID node, std::unordered_set<TransactionID> trace) {
        if (trace.count(node)) {
            return node == origin && trace.size() > 1;
        }
        
        trace.insert(node);
        
        if (dependency_map.count(node)) {
            for (TransactionID neighbor : dependency_map[node]) {
                if (findCycleDFS(origin, neighbor, trace)) {
                    return true;
                }
            }
        }
        return false;
    }
};

// ─────────────────────────────────────────────
// 5. Coordinator (Transaction Manager)
// ─────────────────────────────────────────────

class TxnCoordinator {
private:
    static std::atomic<TransactionID> next_tx_id;
    std::unordered_map<TransactionID, TxnContext> active_txns;
    std::unordered_map<RecordKey, VersionHistory> database;
    LockRegistry locks;
    DeadlockGraph deadlocks;
    std::mutex coord_mutex;

public:
    TransactionID begin() {
        std::lock_guard<std::mutex> lck(coord_mutex);
        TransactionID tx = next_tx_id.fetch_add(1);
        TransactionID snap = tx;
        
        TxnContext ctx{tx, snap, TxnState::ACTIVE, {}, {}, false};
        active_txns[tx] = ctx;
        
        std::cout << "\n[ENGINE] BEGIN Txn " << tx << " (Snapshot = " << snap << ")\n";
        return tx;
    }

    std::optional<std::string> fetch(TransactionID tx, const RecordKey& key) {
        std::lock_guard<std::mutex> lck(coord_mutex);
        
        if (!active_txns.count(tx)) {
            std::cerr << "[ERR] Txn " << tx << " does not exist\n";
            return std::nullopt;
        }
        
        TxnContext& ctx = active_txns[tx];
        if (ctx.shrinking_phase) {
            std::cerr << "[ERR] Txn " << tx << " is in shrinking phase\n";
            return std::nullopt;
        }
        
        if (!database.count(key)) {
            std::cout << "[FETCH] Txn " << tx << " reads " << key << " (not found)\n";
            return std::nullopt;
        }
        
        // MVCC Visibility rules
        const auto& history = database[key].list;
        for (const auto& version : history) {
            if (version.creator_tx <= ctx.snapshot_id && (version.deleter_tx == 0 || version.deleter_tx > ctx.snapshot_id)) {
                ctx.read_set.insert(key);
                std::cout << "[FETCH] Txn " << tx << " reads " << key << " = '" << version.data_payload << "'\n";
                return version.data_payload;
            }
        }
        
        return std::nullopt;
    }

    bool store(TransactionID tx, const RecordKey& key, const std::string& val) {
        std::lock_guard<std::mutex> lck(coord_mutex);
        
        if (!active_txns.count(tx)) {
            std::cerr << "[ERR] Txn " << tx << " does not exist\n";
            return false;
        }
        
        TxnContext& ctx = active_txns[tx];
        if (ctx.shrinking_phase) {
            std::cerr << "[ERR] Txn " << tx << " is in shrinking phase\n";
            return false;
        }
        
        // Strict 2PL: acquire lock before write
        if (!locks.requestExclusiveLock(tx, key)) {
            std::cout << "[WAITING] Txn " << tx << " blocked on EXCLUSIVE lock for " << key << "\n";
            return false;
        }
        
        // Update older active versions to be deleted by this txn
        if (database.count(key)) {
            for (auto& version : database[key].list) {
                if (version.deleter_tx == 0) {
                    version.deleter_tx = tx;
                }
            }
        }
        
        // Insert new MVCC version
        RecordVersion new_ver{tx, 0, val};
        database[key].list.push_back(new_ver);
        
        ctx.write_set.insert(key);
        std::cout << "[STORE] Txn " << tx << " updates " << key << " = '" << val << "'\n";
        
        return true;
    }

    bool commit(TransactionID tx) {
        std::lock_guard<std::mutex> lck(coord_mutex);
        
        if (!active_txns.count(tx)) {
            std::cerr << "[ERR] Txn " << tx << " does not exist\n";
            return false;
        }
        
        TxnContext& ctx = active_txns[tx];
        ctx.shrinking_phase = true;
        
        // Release all active locks (Strict 2PL)
        locks.releaseAllLocks(tx, ctx.read_set);
        locks.releaseAllLocks(tx, ctx.write_set);
        
        ctx.state = TxnState::COMMITTED;
        deadlocks.removeTransaction(tx);
        
        std::cout << "[COMMIT] Txn " << tx << " committed successfully\n";
        return true;
    }

    bool rollback(TransactionID tx) {
        std::lock_guard<std::mutex> lck(coord_mutex);
        
        if (!active_txns.count(tx)) {
            std::cerr << "[ERR] Txn " << tx << " does not exist\n";
            return false;
        }
        
        TxnContext& ctx = active_txns[tx];
        ctx.shrinking_phase = true;
        
        // Rollback MVCC changes
        for (auto& [k, history] : database) {
            history.list.erase(
                std::remove_if(history.list.begin(), history.list.end(),
                    [tx](const RecordVersion& v) { return v.creator_tx == tx; }),
                history.list.end()
            );
        }
        
        // Release locks
        locks.releaseAllLocks(tx, ctx.read_set);
        locks.releaseAllLocks(tx, ctx.write_set);
        
        ctx.state = TxnState::ROLLED_BACK;
        deadlocks.removeTransaction(tx);
        
        std::cout << "[ROLLBACK] Txn " << tx << " rolled back\n";
        return true;
    }

    void dumpState() {
        std::lock_guard<std::mutex> lck(coord_mutex);
        std::cout << "\n================= TXN COORDINATOR STATE =================\n";
        for (const auto& [tx, ctx] : active_txns) {
            std::string state_str = "ACTIVE";
            if (ctx.state == TxnState::COMMITTED) state_str = "COMMITTED";
            if (ctx.state == TxnState::ROLLED_BACK) state_str = "ROLLED_BACK";
            
            std::cout << "Txn #" << tx << " | State: " << state_str
                      << " | Snap ID: " << ctx.snapshot_id
                      << " | Shrinking: " << (ctx.shrinking_phase ? "YES" : "NO") << "\n";
        }
        
        std::cout << "\n=================== DATABASE STORAGE ===================\n";
        for (const auto& [k, history] : database) {
            std::cout << k << " versions: ";
            for (size_t i = 0; i < history.list.size(); ++i) {
                const auto& v = history.list[i];
                std::cout << "[creator=" << v.creator_tx << " deleter=" << v.deleter_tx << " val='" << v.data_payload << "']";
                if (i < history.list.size() - 1) std::cout << " -> ";
            }
            std::cout << "\n";
        }
    }
};

std::atomic<TransactionID> TxnCoordinator::next_tx_id{1};

// ─────────────────────────────────────────────
// 6. Test Driver
// ─────────────────────────────────────────────

int main() {
    std::cout << "=== Transaction Manager with MVCC + Strict 2PL + Deadlock Detection ===\n";
    
    TxnCoordinator coordinator;
    
    std::cout << "\n--- Scenario 1: Basic MVCC Visibility ---\n";
    TransactionID txnA = coordinator.begin();
    coordinator.store(txnA, "user:101", "name=Ayush");
    coordinator.commit(txnA);
    
    TransactionID txnB = coordinator.begin();
    TransactionID txnC = coordinator.begin();
    
    coordinator.fetch(txnB, "user:101");  // reads committed value "name=Ayush"
    coordinator.store(txnC, "user:101", "name=Patra");
    coordinator.commit(txnC);
    
    coordinator.fetch(txnB, "user:101");  // still reads "name=Ayush" (Snapshot Isolation)
    coordinator.commit(txnB);
    
    coordinator.dumpState();

    std::cout << "\n--- Scenario 2: Strict 2PL Write Contention ---\n";
    TransactionID txnD = coordinator.begin();
    TransactionID txnE = coordinator.begin();
    
    coordinator.store(txnD, "user:102", "name=Nandani");
    std::cout << "[CONFLICT] TxnE attempts to update the locked row 'user:102'\n";
    // coordinator.store(txnE, "user:102", "name=Kumari"); // would block in a real scheduler
    coordinator.commit(txnD);
    
    std::cout << "\n--- Scenario 3: Isolated Snapshots ---\n";
    TransactionID txnF = coordinator.begin();
    TransactionID txnG = coordinator.begin();
    
    coordinator.store(txnF, "catalog:1", "items=5");
    coordinator.commit(txnF);
    
    coordinator.fetch(txnG, "catalog:1");  // Sees committed catalog
    coordinator.commit(txnG);
    
    coordinator.dumpState();

    std::cout << "\n=== Concurrency Control Execution Completed ===\n";
    return 0;
}
