#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <list>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <thread>
#include <stdexcept>
#include <optional>
#include <atomic>
#include <string>
#include <chrono>
#include <functional>

// ============================================================================
// Core Datatypes and Configurations
// ============================================================================

using TxnId = uint64_t;
using RecordKey = std::string;

enum class TransactionState { ACTIVE, COMMITTED, ROLLED_BACK };
enum class LockMode { SHARED, EXCLUSIVE };

struct TxnMetadata {
    TxnId txn_id;
    TxnId snapshot_id;
    TransactionState state = TransactionState::ACTIVE;
    bool is_shrinking = false;
};

struct DataVersion {
    std::string payload;
    TxnId xmin_created;
    TxnId xmax_deleted;
};

struct LockIntent {
    TxnId txn_id;
    LockMode mode;
    bool is_granted = false;
};

struct LockRequestQueue {
    std::list<LockIntent> requests;
    std::mutex queue_mutex;
    std::condition_variable cv;
};

// Custom exception class for deadlock scenarios
class TransactionDeadlockException : public std::runtime_error {
public:
    explicit TransactionDeadlockException(TxnId id)
        : std::runtime_error("Deadlock condition identified: Aborting Txn " + std::to_string(id)) {}
};

// ============================================================================
// Database Concurrency Engine
// ============================================================================

class DatabaseEngine {
private:
    // Global Txn Tracking State
    std::atomic<TxnId> id_generator{1};
    std::mutex txn_registry_mutex;
    std::unordered_map<TxnId, TxnMetadata> txn_registry;

    // MVCC Storage Engine State
    std::mutex storage_mutex;
    std::unordered_map<RecordKey, std::list<DataVersion>> version_store;

    // Lock Manager State
    std::mutex lock_mgr_mutex;
    std::unordered_map<RecordKey, LockRequestQueue> lock_table;
    std::unordered_map<TxnId, std::unordered_set<TxnId>> dependency_graph;

private:
    // Internal state query helpers
    bool check_committed(TxnId id) {
        std::lock_guard<std::mutex> lock(txn_registry_mutex);
        auto it = txn_registry.find(id);
        return it != txn_registry.end() && it->second.state == TransactionState::COMMITTED;
    }

    // Graph cycle evaluation for deadlock safety
    bool evaluate_cycle_dfs(TxnId current, 
                            std::unordered_map<TxnId, std::unordered_set<TxnId>>& graph,
                            std::unordered_set<TxnId>& visited, 
                            std::unordered_set<TxnId>& recursion_stack) {
        visited.insert(current);
        recursion_stack.insert(current);

        for (TxnId neighbor : graph[current]) {
            if (visited.find(neighbor) == visited.end()) {
                if (evaluate_cycle_dfs(neighbor, graph, visited, recursion_stack)) {
                    return true;
                }
            } else if (recursion_stack.find(neighbor) != recursion_stack.end()) {
                return true;
            }
        }

        recursion_stack.erase(current);
        return false;
    }

    bool is_deadlocked(TxnId start_node, std::unordered_map<TxnId, std::unordered_set<TxnId>>& graph) {
        std::unordered_set<TxnId> visited;
        std::unordered_set<TxnId> recursion_stack;
        return evaluate_cycle_dfs(start_node, graph, visited, recursion_stack);
    }

    // MVCC Visibility Resolution Rule Engine
    bool determine_visibility(const DataVersion& version, TxnId snapshot_id, TxnId reader_id) {
        bool created_visible = (version.xmin_created == reader_id) || 
                               (check_committed(version.xmin_created) && version.xmin_created < snapshot_id);

        if (!created_visible) return false;
        if (version.xmax_deleted == 0) return true;

        bool deleted_visible = (version.xmax_deleted == reader_id) || 
                               (check_committed(version.xmax_deleted) && version.xmax_deleted < snapshot_id);

        return !deleted_visible;
    }

    // Strict Two-Phase Locking Hook
    void request_lock(const RecordKey& key, TxnId id, LockMode mode) {
        LockRequestQueue& queue = lock_table[key];
        std::unique_lock<std::mutex> unique_lock(queue.queue_mutex);

        queue.requests.push_back({id, mode, false});
        auto current_req_it = std::prev(queue.requests.end());

        while (true) {
            bool has_conflict = false;
            std::unordered_set<TxnId> blockers;

            for (auto it = queue.requests.begin(); it != current_req_it; ++it) {
                if (!it->is_granted) continue;

                if (mode == LockMode::EXCLUSIVE || it->mode == LockMode::EXCLUSIVE) {
                    has_conflict = true;
                    blockers.insert(it->txn_id);
                }
            }

            if (!has_conflict) {
                current_req_it->is_granted = true;
                std::lock_guard<std::mutex> lock(lock_mgr_mutex);
                dependency_graph.erase(id);
                return;
            }

            {
                std::lock_guard<std::mutex> lock(lock_mgr_mutex);
                dependency_graph[id] = blockers;

                if (is_deadlocked(id, dependency_graph)) {
                    queue.requests.erase(current_req_it);
                    throw TransactionDeadlockException(id);
                }
            }

            queue.cv.wait(unique_lock);
        }
    }

    void release_all_locks(TxnId id) {
        {
            std::lock_guard<std::mutex> lock(txn_registry_mutex);
            txn_registry[id].is_shrinking = true;
        }

        for (auto& [key, queue] : lock_table) {
            std::lock_guard<std::mutex> lock(queue.queue_mutex);
            queue.requests.remove_if([&](const LockIntent& req) { return req.txn_id == id; });
            queue.cv.notify_all();
        }

        std::lock_guard<std::mutex> lock(lock_mgr_mutex);
        dependency_graph.erase(id);
    }

public:
    // ============================================================================
    // Transaction Lifecycle & Command APIs
    // ============================================================================

    TxnId begin_transaction() {
        std::lock_guard<std::mutex> lock(txn_registry_mutex);
        TxnId assigned_id = id_generator.fetch_add(1);
        txn_registry[assigned_id] = {assigned_id, assigned_id, TransactionState::ACTIVE, false};
        return assigned_id;
    }

    std::optional<std::string> execute_read(TxnId id, const RecordKey& key) {
        request_lock(key, id, LockMode::SHARED);
        std::lock_guard<std::mutex> lock(storage_mutex);

        TxnId snapshot;
        {
            std::lock_guard<std::mutex> lock_tx(txn_registry_mutex);
            snapshot = txn_registry[id].snapshot_id;
        }

        if (version_store.find(key) == version_store.end()) return std::nullopt;

        for (const auto& version : version_store[key]) {
            if (determine_visibility(version, snapshot, id)) {
                return version.payload;
            }
        }
        return std::nullopt;
    }

    void execute_insert(TxnId id, const RecordKey& key, const std::string& data) {
        request_lock(key, id, LockMode::EXCLUSIVE);
        std::lock_guard<std::mutex> lock(storage_mutex);
        version_store[key].push_front({data, id, 0});
    }

    void execute_update(TxnId id, const RecordKey& key, const std::string& data) {
        request_lock(key, id, LockMode::EXCLUSIVE);
        std::lock_guard<std::mutex> lock(storage_mutex);

        TxnId snapshot;
        {
            std::lock_guard<std::mutex> lock_tx(txn_registry_mutex);
            snapshot = txn_registry[id].snapshot_id;
        }

        if (version_store.find(key) != version_store.end()) {
            for (auto& version : version_store[key]) {
                if (determine_visibility(version, snapshot, id) && version.xmax_deleted == 0) {
                    version.xmax_deleted = id;
                    break;
                }
            }
        }
        version_store[key].push_front({data, id, 0});
    }

    void execute_delete(TxnId id, const RecordKey& key) {
        request_lock(key, id, LockMode::EXCLUSIVE);
        std::lock_guard<std::mutex> lock(storage_mutex);

        TxnId snapshot;
        {
            std::lock_guard<std::mutex> lock_tx(txn_registry_mutex);
            snapshot = txn_registry[id].snapshot_id;
        }

        if (version_store.find(key) == version_store.end()) return;

        for (auto& version : version_store[key]) {
            if (determine_visibility(version, snapshot, id) && version.xmax_deleted == 0) {
                version.xmax_deleted = id;
                return;
            }
        }
    }

    void commit_transaction(TxnId id) {
        {
            std::lock_guard<std::mutex> lock(txn_registry_mutex);
            txn_registry[id].state = TransactionState::COMMITTED;
        }
        release_all_locks(id);
        std::cout << "[Transaction " << id << "] Processed COMMIT successfully.\n";
    }

    void abort_transaction(TxnId id) {
        {
            std::lock_guard<std::mutex> lock(storage_mutex);
            for (auto& [key, chain] : version_store) {
                for (auto& version : chain) {
                    if (version.xmin_created == id) version.xmax_deleted = id;
                    if (version.xmax_deleted == id) version.xmax_deleted = 0;
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(txn_registry_mutex);
            txn_registry[id].state = TransactionState::ROLLED_BACK;
        }
        release_all_locks(id);
        std::cout << "[Transaction " << id << "] Processed ABORT/ROLLBACK operation.\n";
    }
};

// ============================================================================
// Execution Framework & Verification Tests
// ============================================================================

void display_record(std::optional<std::string> val, TxnId id, const RecordKey& key) {
    std::cout << "[Transaction " << id << "] Querying key '" << key << "' -> ";
    if (val) {
        std::cout << "Value: \"" << *val << "\"\n";
    } else {
        std::cout << "State: <Non-Visible/Does Not Exist>\n";
    }
}

int main() {
    DatabaseEngine db;

    std::cout << "=======================================\n";
    std::cout << " Running Isolated MVCC Test Case      \n";
    std::cout << "=======================================\n";

    TxnId client_1 = db.begin_transaction();
    db.execute_insert(client_1, "savings_acc", "1000");
    db.commit_transaction(client_1);

    TxnId client_2 = db.begin_transaction();
    TxnId client_3 = db.begin_transaction();

    db.execute_update(client_3, "savings_acc", "2000");
    db.commit_transaction(client_3);

    // Snapshot isolation validation: client_2 shouldn't see client_3 updates yet
    display_record(db.execute_read(client_2, "savings_acc"), client_2, "savings_acc");
    db.commit_transaction(client_2);

    std::cout << "\n=======================================\n";
    std::cout << " Running Lock Conflict Test Case      \n";
    std::cout << "=======================================\n";

    TxnId client_4 = db.begin_transaction();
    TxnId client_5 = db.begin_transaction();

    // Concurrent shared lock confirmation
    display_record(db.execute_read(client_4, "savings_acc"), client_4, "savings_acc");
    display_record(db.execute_read(client_5, "savings_acc"), client_5, "savings_acc");

    db.commit_transaction(client_4);
    db.commit_transaction(client_5);

    std::cout << "\n=======================================\n";
    std::cout << " Database Engine Core Testing Complete \n";
    std::cout << "=======================================\n";

    return 0;
}