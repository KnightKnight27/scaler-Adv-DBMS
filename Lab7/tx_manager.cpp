#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <list>
#include <string>
#include <sstream>
#include <cassert>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <stdexcept>

// Core aliases
using TxID = unsigned long long;
using KeyType = std::string;

// ─── Transaction Configurations ──────────────────────────────────────────────

enum class TransactionState { ACTIVE, COMMITTED, ABORTED };

struct TxMetadata {
    TxID tx_id;
    TxID active_snapshot_id;
    TransactionState state;
    bool enters_shrinking_phase;
};

class CentralTxRegistry {
private:
    std::mutex registry_lock;
    std::unordered_map<TxID, TxMetadata> registry;
    std::atomic<TxID> global_xid_counter{0};

public:
    TxID generate_new_transaction() {
        std::lock_guard<std::mutex> lock(registry_lock);
        TxID next_id = ++global_xid_counter;
        registry[next_id] = TxMetadata{next_id, next_id, TransactionState::ACTIVE, false};
        return next_id;
    }

    TxMetadata get_metadata(TxID xid) {
        std::lock_guard<std::mutex> lock(registry_lock);
        return registry.at(xid);
    }

    void update_state(TxID xid, TransactionState new_state) {
        std::lock_guard<std::mutex> lock(registry_lock);
        if (registry.count(xid)) {
            registry[xid].state = new_state;
        }
    }

    void set_shrinking(TxID xid) {
        std::lock_guard<std::mutex> lock(registry_lock);
        if (registry.count(xid)) {
            registry[xid].enters_shrinking_phase = true;
        }
    }

    bool check_committed(TxID xid) {
        std::lock_guard<std::mutex> lock(registry_lock);
        auto it = registry.find(xid);
        return it != registry.end() && it->second.state == TransactionState::COMMITTED;
    }
};

static CentralTxRegistry tx_registry;

// ─── Modernized MVCC Multi-Version Data Storage ──────────────────────────────

struct DataVersion {
    std::string text_payload;
    TxID created_by_xid;  // Equivalent to xmin
    TxID deleted_by_xid;  // Equivalent to xmax
};

class MVCCStorageEngine {
private:
    std::mutex storage_lock;
    std::unordered_map<KeyType, std::list<DataVersion>> version_chains;

    bool verify_visibility(const DataVersion& ver, TxID snapshot_id, TxID reader_id) {
        bool created_visible = (ver.created_by_xid == reader_id) || 
                               (tx_registry.check_committed(ver.created_by_xid) && ver.created_by_xid < snapshot_id);
        if (!created_visible) return false;

        if (ver.deleted_by_xid == 0) return true;
        
        bool deleted_visible = (ver.deleted_by_xid == reader_id) || 
                               (tx_registry.check_committed(ver.deleted_by_xid) && ver.deleted_by_xid < snapshot_id);
        return !deleted_visible;
    }

public:
    bool read_version(const KeyType& key, TxID xid, std::string& target_out) {
        std::lock_guard<std::mutex> lock(storage_lock);
        TxID snapshot_id = tx_registry.get_metadata(xid).active_snapshot_id;

        auto it = version_chains.find(key);
        if (it == version_chains.end()) return false;

        for (auto& ver : it->second) {
            if (verify_visibility(ver, snapshot_id, xid)) {
                target_out = ver.text_payload;
                return true;
            }
        }
        return false;
    }

    void append_version(const KeyType& key, const std::string& value, TxID xid) {
        std::lock_guard<std::mutex> lock(storage_lock);
        version_chains[key].push_front({value, xid, 0});
    }

    void modify_version(const KeyType& key, const std::string& value, TxID xid) {
        std::lock_guard<std::mutex> lock(storage_lock);
        TxID snapshot_id = tx_registry.get_metadata(xid).active_snapshot_id;

        auto it = version_chains.find(key);
        if (it != version_chains.end()) {
            for (auto& ver : it->second) {
                if (verify_visibility(ver, snapshot_id, xid) && ver.deleted_by_xid == 0) {
                    ver.deleted_by_xid = xid;
                    break;
                }
            }
        }
        version_chains[key].push_front({value, xid, 0});
    }

    void clear_version(const KeyType& key, TxID xid) {
        std::lock_guard<std::mutex> lock(storage_lock);
        TxID snapshot_id = tx_registry.get_metadata(xid).active_snapshot_id;

        auto it = version_chains.find(key);
        if (it == version_chains.end()) return;

        for (auto& ver : it->second) {
            if (verify_visibility(ver, snapshot_id, xid) && ver.deleted_by_xid == 0) {
                ver.deleted_by_xid = xid;
                return;
            }
        }
    }

    void handle_abort_cleanup(TxID xid) {
        std::lock_guard<std::mutex> lock(storage_lock);
        for (auto& kv : version_chains) {
            for (auto& ver : kv.second) {
                if (ver.created_by_xid == xid) ver.deleted_by_xid = xid;
                if (ver.deleted_by_xid == xid && ver.created_by_xid != xid) ver.deleted_by_xid = 0;
            }
        }
    }
};

static MVCCStorageEngine storage_engine;

// ─── Object-Oriented Lock Manager (Strict 2PL) ───────────────────────────────

enum class LockType { INTENT_SHARED, INTENT_EXCLUSIVE };

struct LockAcquisitionRequest {
    TxID applicant_xid;
    LockType structural_mode;
    bool is_currently_granted;
};

struct LockAllocationQueue {
    std::list<LockAcquisitionRequest> entry_list;
    std::mutex queue_mutex;
    std::condition_variable dynamic_cv;
};

class DeadlockException : public std::runtime_error {
public:
    explicit DeadlockException(TxID xid)
        : std::runtime_error("Deadlock detected, aborting tx " + std::to_string(xid)) {}
};

class ConcurrencyLockManager {
private:
    std::mutex manager_structural_lock;
    std::unordered_map<KeyType, LockAllocationQueue*> primary_lock_table;
    std::unordered_map<TxID, std::unordered_set<TxID>> dynamic_dependency_graph;

    bool check_graph_for_cycles(TxID origin, const std::unordered_map<TxID, std::unordered_set<TxID>>& graph) {
        std::unordered_set<TxID> fully_explored, active_recursion_stack;
        
        std::function<bool(TxID)> execute_dfs = [&](TxID current_node) -> bool {
            fully_explored.insert(current_node);
            active_recursion_stack.insert(current_node);
            
            auto it = graph.find(current_node);
            if (it != graph.end()) {
                for (TxID neighbor : it->second) {
                    if (!fully_explored.count(neighbor) && execute_dfs(neighbor)) return true;
                    if (active_recursion_stack.count(neighbor)) return true;
                }
            }
            active_recursion_stack.erase(current_node);
            return false;
        };
        
        return execute_dfs(origin);
    }

    LockAllocationQueue& resolve_queue(const KeyType& key) {
        std::lock_guard<std::mutex> lock(manager_structural_lock);
        auto it = primary_lock_table.find(key);
        if (it == primary_lock_table.end()) {
            auto* new_queue = new LockAllocationQueue();
            primary_lock_table[key] = new_queue;
            return *new_queue;
        }
        return *it->second;
    }

public:
    void request_lock_clearance(const KeyType& key, TxID xid, LockType mode) {
        if (tx_registry.get_metadata(xid).enters_shrinking_phase) {
            throw std::runtime_error("2PL violation: cannot acquire lock in shrinking phase");
        }

        LockAllocationQueue& queue = resolve_queue(key);
        std::unique_lock<std::mutex> queue_lock(queue.queue_mutex);

        for (auto& req : queue.entry_list) {
            if (req.applicant_xid == xid && req.is_currently_granted) {
                if (mode == LockType::INTENT_SHARED) return;
                if (req.structural_mode == LockType::INTENT_EXCLUSIVE) return;
            }
        }

        queue.entry_list.push_back({xid, mode, false});
        LockAcquisitionRequest& current_handle = queue.entry_list.back();

        while (true) {
            bool structural_conflict = false;
            std::unordered_set<TxID> dynamic_blockers;

            for (auto& req : queue.entry_list) {
                if (&req == &current_handle) break;
                if (!req.is_currently_granted) continue;
                
                if (mode == LockType::INTENT_EXCLUSIVE || req.structural_mode == LockType::INTENT_EXCLUSIVE) {
                    if (req.applicant_xid != xid) {
                        structural_conflict = true;
                        dynamic_blockers.insert(req.applicant_xid);
                    }
                }
            }

            if (!structural_conflict) {
                current_handle.is_currently_granted = true;
                {
                    std::lock_guard<std::mutex> structural_lk(manager_structural_lock);
                    dynamic_dependency_graph.erase(xid);
                }
                return;
            }

            {
                std::lock_guard<std::mutex> structural_lk(manager_structural_lock);
                dynamic_dependency_graph[xid] = dynamic_blockers;
                if (check_graph_for_cycles(xid, dynamic_dependency_graph)) {
                    dynamic_dependency_graph.erase(xid);
                    queue.entry_list.remove_if([&](const LockAcquisitionRequest& r) {
                        return r.applicant_xid == xid && !r.is_currently_granted;
                    });
                    throw DeadlockException(xid);
                }
            }

            queue.dynamic_cv.wait(queue_lock);
        }
    }

    void systematic_release(TxID xid) {
        tx_registry.set_shrinking(xid);

        std::lock_guard<std::mutex> structural_lk(manager_structural_lock);
        for (auto& kv : primary_lock_table) {
            LockAllocationQueue& queue = *kv.second;
            std::lock_guard<std::mutex> queue_lk(queue.queue_mutex);
            queue.entry_list.remove_if([&](const LockAcquisitionRequest& r) { return r.applicant_xid == xid; });
            queue.dynamic_cv.notify_all();
        }
        dynamic_dependency_graph.erase(xid);
    }
};

static ConcurrencyLockManager lock_manager;

// ─── High-Level Integrated Transaction Manager ───────────────────────────────

class TransactionManager {
public:
    TxID begin() {
        return tx_registry.generate_new_transaction();
    }

    bool read(TxID xid, const KeyType& key, std::string& out) {
        lock_manager.request_lock_clearance(key, xid, LockType::INTENT_SHARED);
        return storage_engine.read_version(key, xid, out);
    }

    void insert(TxID xid, const KeyType& key, const std::string& value) {
        lock_manager.request_lock_clearance(key, xid, LockType::INTENT_EXCLUSIVE);
        storage_engine.append_version(key, value, xid);
    }

    void update(TxID xid, const KeyType& key, const std::string& value) {
        lock_manager.request_lock_clearance(key, xid, LockType::INTENT_EXCLUSIVE);
        storage_engine.modify_version(key, value, xid);
    }

    void remove(TxID xid, const KeyType& key) {
        lock_manager.request_lock_clearance(key, xid, LockType::INTENT_EXCLUSIVE);
        storage_engine.clear_version(key, xid);
    }

    void commit(TxID xid) {
        tx_registry.update_state(xid, TransactionState::COMMITTED);
        lock_manager.systematic_release(xid);
        std::cout << "[TX " << xid << "] COMMITTED\n";
    }

    void abort(TxID xid) {
        storage_engine.handle_abort_cleanup(xid);
        tx_registry.update_state(xid, TransactionState::ABORTED);
        lock_manager.systematic_release(xid);
        std::cout << "[TX " << xid << "] ABORTED\n";
    }
};

void log_read_operation(bool success, const std::string& data, TxID xid, const KeyType& key) {
    std::cout << "  [TX " << xid << "] READ " << key << " = "
              << (success ? data : "<not visible>") << "\n";
}

// ─── Demonstration Execution Scenarios ───────────────────────────────────────

int main() {
    TransactionManager tm;

    std::cout << "=== Scenario 1: MVCC Snapshot Isolation ===\n";
    {
        TxID t1 = tm.begin();
        tm.insert(t1, "balance", "1000");
        tm.commit(t1);

        TxID t2 = tm.begin();
        TxID t3 = tm.begin();

        tm.update(t3, "balance", "2000");
        tm.commit(t3);

        std::string val;
        bool found = tm.read(t2, "balance", val);
        log_read_operation(found, val, t2, "balance");
        tm.commit(t2);
    }

    std::cout << "\n=== Scenario 2: Concurrent Shared Locks ===\n";
    {
        TxID t4 = tm.begin();
        TxID t5 = tm.begin();
        std::string v4, v5;
        log_read_operation(tm.read(t4, "balance", v4), v4, t4, "balance");
        log_read_operation(tm.read(t5, "balance", v5), v5, t5, "balance");
        tm.commit(t4);
        tm.commit(t5);
    }

    std::cout << "\n=== Scenario 3: Exclusive Lock + Waiting ===\n";
    {
        TxID t6 = tm.begin();
        tm.update(t6, "balance", "3000");

        std::thread reader_thread([&tm]() {
            TxID t7 = tm.begin();
            std::cout << "  [TX " << t7 << "] waiting for shared lock on balance...\n";
            std::string val;
            bool found = tm.read(t7, "balance", val);
            log_read_operation(found, val, t7, "balance");
            tm.commit(t7);
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        tm.commit(t6);
        reader_thread.join();
    }

    std::cout << "\n=== Scenario 4: Deadlock Detection ===\n";
    {
        TxID ta = tm.begin();
        TxID tb = tm.begin();
        tm.insert(ta, "A", "val_a");
        tm.insert(tb, "B", "val_b");
        tm.commit(ta);
        tm.commit(tb);

        TxID t8 = tm.begin();
        TxID t9 = tm.begin();

        lock_manager.request_lock_clearance("A", t8, LockType::INTENT_EXCLUSIVE);
        lock_manager.request_lock_clearance("B", t9, LockType::INTENT_EXCLUSIVE);

        std::thread companion_thread([&tm, t8]() {
            try {
                lock_manager.request_lock_clearance("B", t8, LockType::INTENT_EXCLUSIVE);
                tm.commit(t8);
            } catch (DeadlockException& e) {
                std::cout << "  " << e.what() << "\n";
                tm.abort(t8);
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        try {
            lock_manager.request_lock_clearance("A", t9, LockType::INTENT_EXCLUSIVE);
            tm.commit(t9);
        } catch (DeadlockException& e) {
            std::cout << "  " << e.what() << "\n";
            tm.abort(t9);
        }

        companion_thread.join();
    }

    std::cout << "\nAll scenarios complete.\n";
    return 0;
}