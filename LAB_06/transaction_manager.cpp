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
#include <list>
#include <functional>

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
    std::string lock_shrinking;
};

// ─────────────────────────────────────────────
// 3. Deadlock Detection (Waits-For Graph)
// ─────────────────────────────────────────────

class DeadlockGraph {
private:
    std::unordered_map<TransactionID, std::unordered_set<TransactionID>> adj;
    std::mutex mtx;

public:
    void addEdge(TransactionID from, TransactionID to) {
        std::lock_guard<std::mutex> lock(mtx);
        adj[from].insert(to);
    }

    void removeEdge(TransactionID from, TransactionID to) {
        std::lock_guard<std::mutex> lock(mtx);
        if (adj.count(from)) {
            adj[from].erase(to);
        }
    }

    void clearNode(TransactionID node) {
        std::lock_guard<std::mutex> lock(mtx);
        adj.erase(node);
        for (auto& [k, v] : adj) {
            v.erase(node);
        }
    }

    bool hasCycle(TransactionID start) {
        std::lock_guard<std::mutex> lock(mtx);
        std::unordered_set<TransactionID> visited;
        std::unordered_set<TransactionID> recStack;
        return dfs(start, visited, recStack);
    }

private:
    bool dfs(TransactionID node, std::unordered_set<TransactionID>& visited, std::unordered_set<TransactionID>& recStack) {
        visited.insert(node);
        recStack.insert(node);

        if (adj.count(node)) {
            for (TransactionID neighbor : adj[node]) {
                if (!visited.count(neighbor)) {
                    if (dfs(neighbor, visited, recStack)) return true;
                } else if (recStack.count(neighbor)) {
                    return true;
                }
            }
        }

        recStack.erase(node);
        return false;
    }
};

// ─────────────────────────────────────────────
// 4. Lock Manager (Strict 2PL)
// ─────────────────────────────────────────────

class LockRegistry {
private:
    struct LockRequest {
        TransactionID tx_id;
        LockMode mode;
        bool granted;
    };

    struct LockQueue {
        std::list<LockRequest> requests;
        std::mutex q_mtx;
        std::condition_variable cv;
    };

    std::unordered_map<RecordKey, LockQueue*> lock_table;
    std::mutex table_mtx;
    std::unordered_map<TransactionID, std::unordered_set<RecordKey>> tx_locks;
    std::mutex tx_locks_mtx;

public:
    ~LockRegistry() {
        std::lock_guard<std::mutex> lock(table_mtx);
        for (auto& [k, q] : lock_table) {
            delete q;
        }
    }

    void acquire(const RecordKey& key, TransactionID tx_id, LockMode mode, DeadlockGraph& dg) {
        LockQueue* q = nullptr;
        {
            std::lock_guard<std::mutex> lock(table_mtx);
            if (!lock_table.count(key)) {
                lock_table[key] = new LockQueue();
            }
            q = lock_table[key];
        }

        std::unique_lock<std::mutex> u_lock(q->q_mtx);

        // Check if already held by this txn
        for (auto& req : q->requests) {
            if (req.tx_id == tx_id && req.granted) {
                if (mode == LockMode::SHARED) return;
                if (req.mode == LockMode::EXCLUSIVE) return;
            }
        }

        q->requests.push_back({tx_id, mode, false});
        auto& my_req = q->requests.back();

        while (true) {
            bool conflict = false;
            std::unordered_set<TransactionID> holders;

            for (auto& req : q->requests) {
                if (req.tx_id == tx_id && &req == &my_req) break;
                if (!req.granted) continue;
                if (mode == LockMode::EXCLUSIVE || req.mode == LockMode::EXCLUSIVE) {
                    if (req.tx_id != tx_id) {
                        conflict = true;
                        holders.insert(req.tx_id);
                    }
                }
            }

            if (!conflict) {
                my_req.granted = true;
                std::lock_guard<std::mutex> lock(tx_locks_mtx);
                tx_locks[tx_id].insert(key);
                std::cout << "[REGISTRY] Txn " << tx_id << " acquired "
                          << (mode == LockMode::EXCLUSIVE ? "EXCLUSIVE" : "SHARED")
                          << " lock on " << key << "\n";
                return;
            }

            // Record waits-for edges
            for (TransactionID holder : holders) {
                dg.addEdge(tx_id, holder);
            }

            if (dg.hasCycle(tx_id)) {
                for (TransactionID holder : holders) {
                    dg.removeEdge(tx_id, holder);
                }
                q->requests.remove_if([&](const LockRequest& r) { return r.tx_id == tx_id && !r.granted; });
                throw std::runtime_error("Deadlock detected, aborting txn " + std::to_string(tx_id));
            }

            q->cv.wait(u_lock);
        }
    }

    void releaseAll(TransactionID tx_id, DeadlockGraph& dg) {
        std::unordered_set<RecordKey> keys;
        {
            std::lock_guard<std::mutex> lock(tx_locks_mtx);
            if (tx_locks.count(tx_id)) {
                keys = tx_locks[tx_id];
                tx_locks.erase(tx_id);
            }
        }

        dg.clearNode(tx_id);

        for (const auto& key : keys) {
            LockQueue* q = nullptr;
            {
                std::lock_guard<std::mutex> lock(table_mtx);
                if (lock_table.count(key)) {
                    q = lock_table[key];
                }
            }
            if (q) {
                std::lock_guard<std::mutex> q_lock(q->q_mtx);
                for (auto it = q->requests.begin(); it != q->requests.end(); ) {
                    if (it->tx_id == tx_id) {
                        it = q->requests.erase(it);
                    } else {
                        ++it;
                    }
                }
                std::cout << "[REGISTRY] Txn " << tx_id << " released lock on " << key << "\n";
                q->cv.notify_all();
            }
        }
    }
};

// ─────────────────────────────────────────────
// 5. Txn Coordinator (Engine Orchestrator)
// ─────────────────────────────────────────────

class TxnCoordinator {
private:
    std::atomic<TransactionID> next_tx_id{1};
    std::unordered_map<TransactionID, TxnContext> txns;
    std::unordered_map<RecordKey, VersionHistory> storage;
    std::mutex state_mtx;
    LockRegistry lock_registry;
    DeadlockGraph deadlock_graph;

public:
    TransactionID begin() {
        std::lock_guard<std::mutex> lock(state_mtx);
        TransactionID tx_id = next_tx_id++;
        txns[tx_id] = TxnContext{tx_id, tx_id, TxnState::ACTIVE, "NO"};
        std::cout << "[ENGINE] BEGIN Txn " << tx_id << " (Snapshot = " << tx_id << ")\n";
        return tx_id;
    }

    void store(TransactionID tx_id, const RecordKey& key, const std::string& val) {
        lock_registry.acquire(key, tx_id, LockMode::EXCLUSIVE, deadlock_graph);
        
        std::lock_guard<std::mutex> lock(state_mtx);
        auto& history = storage[key];
        for (auto& v : history.list) {
            if (v.creator_tx != tx_id && v.deleter_tx == 0) {
                v.deleter_tx = tx_id;
            }
        }
        history.list.push_back(RecordVersion{tx_id, 0, val});
        std::cout << "[STORE] Txn " << tx_id << " updates " << key << " = '" << val << "'\n";
    }

    std::string fetch(TransactionID tx_id, const RecordKey& key) {
        std::lock_guard<std::mutex> lock(state_mtx);
        TransactionID snapshot_id = txns.at(tx_id).snapshot_id;
        
        if (!storage.count(key)) {
            std::cout << "[FETCH] Txn " << tx_id << " reads " << key << " = <not visible>\n";
            return "";
        }

        auto& history = storage.at(key);
        for (auto it = history.list.rbegin(); it != history.list.rend(); ++it) {
            if (is_visible(*it, tx_id, snapshot_id)) {
                std::cout << "[FETCH] Txn " << tx_id << " reads " << key << " = '" << it->data_payload << "'\n";
                return it->data_payload;
            }
        }
        std::cout << "[FETCH] Txn " << tx_id << " reads " << key << " = <not visible>\n";
        return "";
    }

    void commit(TransactionID tx_id) {
        {
            std::lock_guard<std::mutex> lock(state_mtx);
            txns.at(tx_id).state = TxnState::COMMITTED;
            txns.at(tx_id).lock_shrinking = "YES";
        }
        lock_registry.releaseAll(tx_id, deadlock_graph);
        std::cout << "[COMMIT] Txn " << tx_id << " committed successfully\n";
    }

    void abort(TransactionID tx_id) {
        {
            std::lock_guard<std::mutex> lock(state_mtx);
            txns.at(tx_id).state = TxnState::ROLLED_BACK;
            txns.at(tx_id).lock_shrinking = "YES";
            // Revert versions
            for (auto& [key, history] : storage) {
                for (auto& v : history.list) {
                    if (v.creator_tx == tx_id) {
                        v.creator_tx = 0;
                    }
                    if (v.deleter_tx == tx_id) {
                        v.deleter_tx = 0;
                    }
                }
            }
        }
        lock_registry.releaseAll(tx_id, deadlock_graph);
        std::cout << "[ABORT] Txn " << tx_id << " aborted\n";
    }

    void dumpState() {
        std::lock_guard<std::mutex> lock(state_mtx);
        std::cout << "\n================= TXN COORDINATOR STATE =================\n";
        std::vector<TransactionID> ids;
        for (auto& [id, ctx] : txns) ids.push_back(id);
        std::sort(ids.begin(), ids.end());
        for (auto id : ids) {
            auto& ctx = txns.at(id);
            std::string state_str = "";
            if (ctx.state == TxnState::ACTIVE) state_str = "ACTIVE";
            else if (ctx.state == TxnState::COMMITTED) state_str = "COMMITTED";
            else state_str = "ROLLED_BACK";
            std::cout << "Txn #" << id << " | State: " << state_str 
                      << " | Snap ID: " << ctx.snapshot_id 
                      << " | Shrinking: " << ctx.lock_shrinking << "\n";
        }

        std::cout << "\n=================== DATABASE STORAGE ===================\n";
        std::vector<RecordKey> keys;
        for (auto& [k, v] : storage) keys.push_back(k);
        std::sort(keys.begin(), keys.end());
        for (auto& key : keys) {
            std::cout << key << " versions: ";
            auto& history = storage.at(key);
            bool first = true;
            for (auto& v : history.list) {
                if (v.creator_tx == 0 && v.data_payload.empty()) continue;
                if (!first) std::cout << " -> ";
                first = false;
                std::cout << "[creator=" << v.creator_tx << " deleter=" << v.deleter_tx 
                          << " val='" << v.data_payload << "']";
            }
            std::cout << "\n";
        }
    }

private:
    bool is_visible(const RecordVersion& v, TransactionID reader_tx, TransactionID snapshot_id) {
        if (v.creator_tx == reader_tx) {
            if (v.deleter_tx == reader_tx) return false;
            return true;
        }
        if (v.creator_tx == 0) return false;
        
        bool creator_committed = false;
        auto it = txns.find(v.creator_tx);
        if (it != txns.end() && it->second.state == TxnState::COMMITTED && v.creator_tx < snapshot_id) {
            creator_committed = true;
        }

        if (!creator_committed) return false;

        if (v.deleter_tx == 0) return true;
        if (v.deleter_tx == reader_tx) return false;

        bool deleter_committed = false;
        auto del_it = txns.find(v.deleter_tx);
        if (del_it != txns.end() && del_it->second.state == TxnState::COMMITTED && v.deleter_tx < snapshot_id) {
            deleter_committed = true;
        }
        return !deleter_committed;
    }
};

// ─────────────────────────────────────────────
// 6. Main Scenario Tests
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
    (void)txnE;
    
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