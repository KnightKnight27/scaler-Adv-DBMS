/*
 * Database Transaction Manager Demo
 * Demonstrating Multi-Version Concurrency Control, Strict 2PL, and Deadlock Cycle Detection.
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <list>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <vector>

using TransactionID = uint64_t;
using RecordKey = std::string;

// ---------------------------------------------------------------------------
// Transaction States and Metadata
// ---------------------------------------------------------------------------

enum class PhaseType { Expanding, Contracting };
enum class StatusResult { InProgress, Finalized, RolledBack };

struct TransactionState {
    TransactionID id;
    TransactionID snapshot_view;
    StatusResult status = StatusResult::InProgress;
    PhaseType phase = PhaseType::Expanding;
};

namespace registry {

std::mutex registry_mtx;
std::atomic<TransactionID> next_id_val{1};
std::unordered_map<TransactionID, TransactionState> active_transactions;

TransactionID begin_transaction() {
    std::lock_guard lock(registry_mtx);
    TransactionID new_tx_id = next_id_val++;
    active_transactions[new_tx_id] = TransactionState{new_tx_id, new_tx_id, StatusResult::InProgress, PhaseType::Expanding};
    return new_tx_id;
}

TransactionState get_snapshot(TransactionID tx_id) {
    std::lock_guard lock(registry_mtx);
    return active_transactions.at(tx_id);
}

void update_status(TransactionID tx_id, StatusResult new_status) {
    std::lock_guard lock(registry_mtx);
    active_transactions.at(tx_id).status = new_status;
}

void trigger_contracting_phase(TransactionID tx_id) {
    std::lock_guard lock(registry_mtx);
    if (active_transactions.count(tx_id)) {
        active_transactions.at(tx_id).phase = PhaseType::Contracting;
    }
}

bool is_finalized(TransactionID tx_id) {
    std::lock_guard lock(registry_mtx);
    auto iterator = active_transactions.find(tx_id);
    return iterator != active_transactions.end() && iterator->second.status == StatusResult::Finalized;
}

} // namespace registry

// ---------------------------------------------------------------------------
// MVCC Storage Engine
// ---------------------------------------------------------------------------

struct DataVersion {
    std::string payload;
    TransactionID author_tx;
    TransactionID deleter_tx;
};

namespace mvcc_storage {

std::mutex storage_mtx;
std::unordered_map<RecordKey, std::list<DataVersion>> table_records;

bool is_visible(const DataVersion& version, TransactionID snapshot_view, TransactionID current_tx) {
    bool is_created_valid = (version.author_tx == current_tx) || 
                            (registry::is_finalized(version.author_tx) && version.author_tx < snapshot_view);
    
    if (!is_created_valid) return false;
    if (version.deleter_tx == 0) return true;

    bool is_deleted_valid = (version.deleter_tx == current_tx) || 
                            (registry::is_finalized(version.deleter_tx) && version.deleter_tx < snapshot_view);
    
    return !is_deleted_valid;
}

std::optional<std::string> fetch_record(const RecordKey& key, TransactionID current_tx) {
    std::lock_guard lock(storage_mtx);
    TransactionID view = registry::get_snapshot(current_tx).snapshot_view;

    auto record_entry = table_records.find(key);
    if (record_entry == table_records.end()) return std::nullopt;

    for (const DataVersion& ver : record_entry->second) {
        if (is_visible(ver, view, current_tx)) return ver.payload;
    }
    return std::nullopt;
}

void add_record(const RecordKey& key, const std::string& data, TransactionID author_tx) {
    std::lock_guard lock(storage_mtx);
    table_records[key].push_front({data, author_tx, 0});
}

void modify_record(const RecordKey& key, const std::string& data, TransactionID author_tx) {
    std::lock_guard lock(storage_mtx);
    TransactionID view = registry::get_snapshot(author_tx).snapshot_view;

    auto record_entry = table_records.find(key);
    if (record_entry != table_records.end()) {
        for (DataVersion& ver : record_entry->second) {
            if (is_visible(ver, view, author_tx) && ver.deleter_tx == 0) {
                ver.deleter_tx = author_tx;
                break;
            }
        }
    }
    table_records[key].push_front({data, author_tx, 0});
}

void delete_record(const RecordKey& key, TransactionID author_tx) {
    std::lock_guard lock(storage_mtx);
    TransactionID view = registry::get_snapshot(author_tx).snapshot_view;

    auto record_entry = table_records.find(key);
    if (record_entry == table_records.end()) return;

    for (DataVersion& ver : record_entry->second) {
        if (is_visible(ver, view, author_tx) && ver.deleter_tx == 0) {
            ver.deleter_tx = author_tx;
            return;
        }
    }
}

void revert_changes(TransactionID failed_tx) {
    std::lock_guard lock(storage_mtx);
    for (auto& [key, history] : table_records) {
        for (DataVersion& ver : history) {
            if (ver.author_tx == failed_tx) ver.deleter_tx = failed_tx;
            else if (ver.deleter_tx == failed_tx) ver.deleter_tx = 0;
        }
    }
}

} // namespace mvcc_storage

// ---------------------------------------------------------------------------
// Lock Manager & Cycle Detection
// ---------------------------------------------------------------------------

enum class LockMode { Shared, Exclusive };

struct PendingLock {
    TransactionID tx_id;
    LockMode mode;
    bool granted = false;
};

struct LockWaitQueue {
    std::list<PendingLock> pending_requests;
    std::mutex queue_mtx;
    std::condition_variable state_changed;
};

namespace lock_manager {

std::mutex manager_mtx;
std::unordered_map<RecordKey, LockWaitQueue> resource_queues;
std::unordered_map<TransactionID, std::unordered_set<TransactionID>> dependency_graph;

class CycleDetectedException : public std::runtime_error {
public:
    explicit CycleDetectedException(TransactionID tx_id)
        : std::runtime_error("Deadlock cycle detected involving transaction " + std::to_string(tx_id)) {}
};

bool detect_cycle(TransactionID start_node,
                  const std::unordered_map<TransactionID, std::unordered_set<TransactionID>>& graph) {
    std::unordered_set<TransactionID> visited_nodes, current_path;

    std::function<bool(TransactionID)> search = [&](TransactionID node) -> bool {
        visited_nodes.insert(node);
        current_path.insert(node);

        auto iterator = graph.find(node);
        if (iterator != graph.end()) {
            for (TransactionID neighbor : iterator->second) {
                if (!visited_nodes.count(neighbor) && search(neighbor)) return true;
                if (current_path.count(neighbor)) return true;
            }
        }

        current_path.erase(node);
        return false;
    };

    return search(start_node);
}

bool has_conflict(LockMode requested, LockMode held) {
    return requested == LockMode::Exclusive || held == LockMode::Exclusive;
}

LockWaitQueue& get_queue(const RecordKey& key) {
    std::lock_guard lock(manager_mtx);
    return resource_queues[key];
}

void request_lock(const RecordKey& key, TransactionID tx_id, LockMode mode) {
    if (registry::get_snapshot(tx_id).phase == PhaseType::Contracting) {
        throw std::runtime_error("Violated 2PL: Cannot acquire locks in contracting phase");
    }

    LockWaitQueue& q = get_queue(key);
    std::unique_lock lock(q.queue_mtx);

    for (const PendingLock& req : q.pending_requests) {
        if (req.tx_id == tx_id && req.granted) {
            if (mode == LockMode::Shared || req.mode == LockMode::Exclusive) return;
        }
    }

    q.pending_requests.push_back({tx_id, mode, false});
    PendingLock* my_request = &q.pending_requests.back();

    while (true) {
        bool is_blocked = false;
        std::unordered_set<TransactionID> blocking_txs;

        for (const PendingLock& prior : q.pending_requests) {
            if (&prior == my_request) break;
            if (!prior.granted) continue;
            if (has_conflict(mode, prior.mode) && prior.tx_id != tx_id) {
                is_blocked = true;
                blocking_txs.insert(prior.tx_id);
            }
        }

        if (!is_blocked) {
            my_request->granted = true;
            std::lock_guard df_lock(manager_mtx);
            dependency_graph.erase(tx_id);
            return;
        }

        {
            std::lock_guard df_lock(manager_mtx);
            dependency_graph[tx_id] = blocking_txs;
            if (detect_cycle(tx_id, dependency_graph)) {
                dependency_graph.erase(tx_id);
                q.pending_requests.remove_if([&](const PendingLock& req) {
                    return req.tx_id == tx_id && !req.granted;
                });
                throw CycleDetectedException(tx_id);
            }
        }

        q.state_changed.wait(lock);
    }
}

void release_all_locks(TransactionID tx_id) {
    registry::trigger_contracting_phase(tx_id);

    std::vector<LockWaitQueue*> target_queues;
    {
        std::lock_guard lock(manager_mtx);
        for (auto& [key, q] : resource_queues) target_queues.push_back(&q);
        dependency_graph.erase(tx_id);
    }

    for (LockWaitQueue* q : target_queues) {
        std::lock_guard lock(q->queue_mtx);
        q->pending_requests.remove_if([&](const PendingLock& req) { return req.tx_id == tx_id; });
        q->state_changed.notify_all();
    }
}

} // namespace lock_manager

// ---------------------------------------------------------------------------
// Database Interface
// ---------------------------------------------------------------------------

class DatabaseTxManager {
public:
    TransactionID begin() { return registry::begin_transaction(); }

    std::optional<std::string> read_record(TransactionID tx, const RecordKey& key) {
        lock_manager::request_lock(key, tx, LockMode::Shared);
        return mvcc_storage::fetch_record(key, tx);
    }

    void write_record(TransactionID tx, const RecordKey& key, const std::string& val) {
        lock_manager::request_lock(key, tx, LockMode::Exclusive);
        if (mvcc_storage::fetch_record(key, tx)) {
            mvcc_storage::modify_record(key, val, tx);
        } else {
            mvcc_storage::add_record(key, val, tx);
        }
    }

    void insert_record(TransactionID tx, const RecordKey& key, const std::string& val) {
        lock_manager::request_lock(key, tx, LockMode::Exclusive);
        mvcc_storage::add_record(key, val, tx);
    }

    void update_record(TransactionID tx, const RecordKey& key, const std::string& val) {
        lock_manager::request_lock(key, tx, LockMode::Exclusive);
        mvcc_storage::modify_record(key, val, tx);
    }

    void remove_record(TransactionID tx, const RecordKey& key) {
        lock_manager::request_lock(key, tx, LockMode::Exclusive);
        mvcc_storage::delete_record(key, tx);
    }

    void manual_lock(const RecordKey& key, TransactionID tx, LockMode mode) {
        lock_manager::request_lock(key, tx, mode);
    }

    void commit_transaction(TransactionID tx) {
        registry::update_status(tx, StatusResult::Finalized);
        lock_manager::release_all_locks(tx);
        std::cout << "[Transaction " << tx << "] SUCCESS\n";
    }

    void abort_transaction(TransactionID tx) {
        mvcc_storage::revert_changes(tx);
        registry::update_status(tx, StatusResult::RolledBack);
        lock_manager::release_all_locks(tx);
        std::cout << "[Transaction " << tx << "] ABORTED\n";
    }
};

// ---------------------------------------------------------------------------
// Executable Demonstration
// ---------------------------------------------------------------------------

static void display_read(const std::optional<std::string>& data, TransactionID tx, const RecordKey& key) {
    std::cout << "  [Transaction " << tx << "] Fetched " << key << " -> "
              << (data ? *data : "NULL") << '\n';
}

int main() {
    DatabaseTxManager engine;

    std::cout << "--- Test 1: Snapshot Isolation in MVCC ---\n";
    {
        TransactionID t1 = engine.begin();
        engine.insert_record(t1, "account", "1000");
        engine.commit_transaction(t1);

        TransactionID t2 = engine.begin();
        TransactionID t3 = engine.begin();

        engine.update_record(t3, "account", "2000");
        engine.commit_transaction(t3);

        display_read(engine.read_record(t2, "account"), t2, "account"); 
        engine.commit_transaction(t2);
    }

    std::cout << "\n--- Test 2: Shared Read Locks ---\n";
    {
        TransactionID t4 = engine.begin();
        TransactionID t5 = engine.begin();
        display_read(engine.read_record(t4, "account"), t4, "account");
        display_read(engine.read_record(t5, "account"), t5, "account");
        engine.commit_transaction(t4);
        engine.commit_transaction(t5);
    }

    std::cout << "\n--- Test 3: Exclusive Lock Blocking ---\n";
    {
        TransactionID t6 = engine.begin();
        engine.update_record(t6, "account", "3000");

        std::thread observer([&] {
            TransactionID t7 = engine.begin();
            std::cout << "  [Transaction " << t7 << "] Waiting for lock on account...\n";
            display_read(engine.read_record(t7, "account"), t7, "account");
            engine.commit_transaction(t7);
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        engine.commit_transaction(t6);
        observer.join();
    }

    std::cout << "\n--- Test 4: Cycle-Based Deadlock Detection ---\n";
    {
        TransactionID ta = engine.begin();
        TransactionID tb = engine.begin();
        engine.insert_record(ta, "Resource1", "R1_Val");
        engine.insert_record(tb, "Resource2", "R2_Val");
        engine.commit_transaction(ta);
        engine.commit_transaction(tb);

        TransactionID t8 = engine.begin();
        TransactionID t9 = engine.begin();

        engine.manual_lock("Resource1", t8, LockMode::Exclusive);
        engine.manual_lock("Resource2", t9, LockMode::Exclusive);

        std::thread circular_requester([&] {
            try {
                engine.manual_lock("Resource2", t8, LockMode::Exclusive);
                engine.commit_transaction(t8);
            } catch (const lock_manager::CycleDetectedException& err) {
                std::cout << "  " << err.what() << '\n';
                engine.abort_transaction(t8);
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        try {
            engine.manual_lock("Resource1", t9, LockMode::Exclusive);
            engine.commit_transaction(t9);
        } catch (const lock_manager::CycleDetectedException& err) {
            std::cout << "  " << err.what() << '\n';
            engine.abort_transaction(t9);
        }

        circular_requester.join();
    }

    std::cout << "\nTesting finished successfully.\n";
    return 0;
}
